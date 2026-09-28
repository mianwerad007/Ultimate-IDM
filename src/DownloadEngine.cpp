#include "DownloadEngine.h"
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <direct.h>
#include <shellapi.h>
#include <filesystem>

// ============================================================
//  Globals
// ============================================================
std::vector<std::shared_ptr<DownloadItem>> g_downloads;
std::mutex g_downloads_mutex;
std::string g_proxy;
std::string g_download_root;
std::string g_history_path;
std::string g_queue_path;
bool g_clipboard_monitor_enabled = false;

static std::atomic<int> g_next_id{1};
static std::atomic<bool> g_clipboard_thread_running{false};
static std::thread g_clipboard_thread;
static std::atomic<bool> g_inbox_thread_running{false};
static std::thread g_inbox_thread;

// ============================================================
//  Unicode helpers
// ============================================================
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring out(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], size_needed);
    return out;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string out(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &out[0], size_needed, NULL, NULL);
    return out;
}

static std::wstring GetExeFolderW() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    return path.substr(0, path.find_last_of(L"\\/"));
}

static std::string GetExeFolder() {
    return WideToUtf8(GetExeFolderW());
}

static bool FileExistsW(const std::wstring& name) {
    struct _stat64i32 buffer;
    return (_wstat(name.c_str(), &buffer) == 0);
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string GetTimestamp() {
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    return std::string(buf);
}

// ============================================================
//  Category / filename helpers
// ============================================================
std::string Engine_GuessCategory(const std::string& url, bool audioOnly) {
    std::string lower = ToLower(url);
    if (lower.find(".zip") != std::string::npos || lower.find(".rar") != std::string::npos ||
        lower.find(".7z") != std::string::npos || lower.find(".exe") != std::string::npos ||
        lower.find(".msi") != std::string::npos)
        return "Compressed";
    if (lower.find(".pdf") != std::string::npos || lower.find(".doc") != std::string::npos ||
        lower.find(".txt") != std::string::npos || lower.find(".pptx") != std::string::npos ||
        lower.find(".xlsx") != std::string::npos)
        return "Documents";
    if (audioOnly) return "Music";
    if (lower.find("tiktok") != std::string::npos || lower.find("youtube") != std::string::npos ||
        lower.find("youtu.be") != std::string::npos || lower.find("instagram") != std::string::npos ||
        lower.find("facebook") != std::string::npos || lower.find("twitter") != std::string::npos ||
        lower.find(".m3u8") != std::string::npos)
        return "Video";
    return "Other";
}

static std::string GetFilenameFromUrl(const std::string& url) {
    size_t last_slash = url.find_last_of("/");
    if (last_slash == std::string::npos) return "file.dat";
    std::string name = url.substr(last_slash + 1);
    size_t param_start = name.find("?");
    if (param_start != std::string::npos) name = name.substr(0, param_start);
    if (name.empty()) name = "file.dat";
    return name;
}

const char* Engine_StatusToString(DownloadStatus s) {
    switch (s) {
        case DownloadStatus::Queued: return "Queued";
        case DownloadStatus::Downloading: return "Downloading";
        case DownloadStatus::Paused: return "Paused";
        case DownloadStatus::Completed: return "Completed";
        case DownloadStatus::Failed: return "Failed";
        case DownloadStatus::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

static bool IsDirectUrl(const std::string& url) {
    std::string lower = ToLower(url);
    return lower.find(".zip") != std::string::npos || lower.find(".exe") != std::string::npos ||
           lower.find(".msi") != std::string::npos || lower.find(".rar") != std::string::npos ||
           lower.find(".7z") != std::string::npos || lower.find(".pdf") != std::string::npos;
}

// ============================================================
//  History
// ============================================================
void Engine_LogHistory(const std::string& url, const std::string& status) {
    std::ofstream file(g_history_path, std::ios_base::app);
    if (file.is_open()) {
        file << "[" << GetTimestamp() << "] " << status << ": " << url << "\n";
    }
}

std::vector<std::string> Engine_ReadHistoryLines() {
    std::vector<std::string> lines;
    std::ifstream file(g_history_path);
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);
    std::reverse(lines.begin(), lines.end());
    return lines;
}

void Engine_ClearHistory() {
    std::ofstream file(g_history_path, std::ofstream::out | std::ofstream::trunc);
}

// ============================================================
//  Queue persistence
// ============================================================
void Engine_SaveQueueToDisk() {
    std::ofstream file(g_queue_path, std::ofstream::out | std::ofstream::trunc);
    std::lock_guard<std::mutex> lock(g_downloads_mutex);
    for (auto& item : g_downloads) {
        if (item->status == DownloadStatus::Queued) {
            file << item->url << "\n";
        }
    }
}

void Engine_LoadQueueFromDisk(QualitySettings q) {
    std::ifstream file(g_queue_path);
    std::string url;
    while (std::getline(file, url)) {
        if (url.length() > 5) Engine_AddDownload(url, q, false);
    }
}

// ============================================================
//  Process execution helpers
// ============================================================
static bool RunProcessStreaming(const std::wstring& cmd, const std::shared_ptr<DownloadItem>& item,
                                 const std::function<void(const std::string&)>& onLine) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return false;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.hStdInput = NULL;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW,
                         NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return false;
    }
    CloseHandle(hWritePipe);

    if (item) {
        std::lock_guard<std::mutex> lock(item->proc_mutex);
        item->hProcess = pi.hProcess;
    }

    std::string buffer;
    char chunk[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, chunk, sizeof(chunk) - 1, &bytesRead, NULL) && bytesRead > 0) {
        chunk[bytesRead] = '\0';
        buffer += chunk;

        size_t pos;
        while ((pos = buffer.find_first_of("\r\n")) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && onLine) onLine(line);
        }

        if (item && item->cancel_requested.load()) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }
    if (!buffer.empty() && onLine) onLine(buffer);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    if (item) {
        std::lock_guard<std::mutex> lock(item->proc_mutex);
        item->hProcess = nullptr;
    }

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}

static bool RunProcessSimple(const std::wstring& cmd) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    return false;
}

// ============================================================
//  yt-dlp progress line parsing
// ============================================================
static void ParseYtDlpLine(const std::shared_ptr<DownloadItem>& item, const std::string& line) {
    if (line.find("[download]") == std::string::npos) return;

    size_t pct_pos = line.find('%');
    if (pct_pos == std::string::npos) return;

    size_t start = pct_pos;
    while (start > 0 && (isdigit((unsigned char)line[start - 1]) || line[start - 1] == '.')) start--;
    if (start == pct_pos) return;

    try {
        double pct = std::stod(line.substr(start, pct_pos - start));
        item->progress = pct;
    } catch (...) {}

    size_t of_pos = line.find(" of ");
    if (of_pos != std::string::npos) {
        size_t at_pos = line.find(" at ", of_pos);
        std::string sizeChunk = (at_pos != std::string::npos)
            ? line.substr(of_pos + 4, at_pos - (of_pos + 4))
            : line.substr(of_pos + 4);
        size_t a = sizeChunk.find_first_not_of(" ~");
        size_t b = sizeChunk.find_last_not_of(" ");
        if (a != std::string::npos && b != std::string::npos) item->size_str = sizeChunk.substr(a, b - a + 1);
    }

    size_t at_pos = line.find(" at ");
    if (at_pos != std::string::npos) {
        size_t eta_pos = line.find(" ETA ", at_pos);
        std::string speedChunk = (eta_pos != std::string::npos)
            ? line.substr(at_pos + 4, eta_pos - (at_pos + 4))
            : line.substr(at_pos + 4);
        size_t a = speedChunk.find_first_not_of(" ");
        size_t b = speedChunk.find_last_not_of(" ");
        if (a != std::string::npos && b != std::string::npos) item->speed_str = speedChunk.substr(a, b - a + 1);
    }

    size_t eta_pos = line.find(" ETA ");
    if (eta_pos != std::string::npos) {
        std::string etaChunk = line.substr(eta_pos + 5);
        size_t b = etaChunk.find_last_not_of(" \t");
        if (b != std::string::npos) item->eta_str = etaChunk.substr(0, b + 1);
    }
}

// ============================================================
//  libcurl direct-download progress callback
// ============================================================
static size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, (FILE*)stream);
}

static int ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    DownloadItem* item = (DownloadItem*)clientp;
    if (item->cancel_requested.load()) return 1;

    if (dltotal > 0) item->progress = ((double)dlnow / (double)dltotal) * 100.0;

    long long nowMs = (long long)GetTickCount64();
    long long lastMs = item->last_time_ms.load();
    if (lastMs != 0 && nowMs > lastMs) {
        double deltaSec = (nowMs - lastMs) / 1000.0;
        if (deltaSec >= 0.3) {
            long long deltaBytes = (long long)dlnow - item->last_bytes.load();
            double bytesPerSec = deltaBytes / deltaSec;
            char buf[64];
            if (bytesPerSec > 1024 * 1024)
                snprintf(buf, sizeof(buf), "%.2f MB/s", bytesPerSec / (1024.0 * 1024.0));
            else
                snprintf(buf, sizeof(buf), "%.1f KB/s", bytesPerSec / 1024.0);
            item->speed_str = buf;
            item->last_bytes = dlnow;
            item->last_time_ms = nowMs;
        }
    } else {
        item->last_bytes = dlnow;
        item->last_time_ms = nowMs;
    }

    if (dltotal > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.1f MB", dltotal / (1024.0 * 1024.0));
        item->size_str = buf;
    }
    return 0;
}

// ============================================================
//  Direct download (plain files) via libcurl
// ============================================================
static void RunDirectDownload(std::shared_ptr<DownloadItem> item) {
    item->status = DownloadStatus::Downloading;
    std::string filename = GetFilenameFromUrl(item->url);
    item->display_name = filename;

    std::string folder = g_download_root + "\\" + item->category;
    _mkdir(folder.c_str());
    std::string fullPath = folder + "\\" + filename;
    item->full_path = fullPath;

    FILE* fp = fopen(fullPath.c_str(), "wb");
    if (!fp) {
        item->status = DownloadStatus::Failed;
        Engine_LogHistory(item->url, "FAILED (cannot open output file)");
        return;
    }

    CURL* curl = curl_easy_init();
    bool ok = false;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, item->url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, item.get());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "UltimateDownloader/2.0");
        if (!g_proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, g_proxy.c_str());

        CURLcode res = curl_easy_perform(curl);
        ok = (res == CURLE_OK);
        curl_easy_cleanup(curl);
    }
    fclose(fp);

    if (item->cancel_requested.load()) {
        item->status = DownloadStatus::Cancelled;
        remove(fullPath.c_str());
        Engine_LogHistory(item->url, "CANCELLED");
        return;
    }

    if (ok) {
        item->progress = 100.0;
        item->status = DownloadStatus::Completed;
        Engine_LogHistory(item->url, "SUCCESS");
    } else {
        item->status = DownloadStatus::Failed;
        Engine_LogHistory(item->url, "FAILED");
    }
}

// ============================================================
//  Universal download (video/audio) via yt-dlp.exe
// ============================================================
static void RunUniversalDownload(std::shared_ptr<DownloadItem> item) {
    item->status = DownloadStatus::Downloading;

    std::string folderUtf8 = g_download_root + "\\" + item->category;
    std::wstring folderW = Utf8ToWide(folderUtf8) + L"\\";
    _wmkdir(folderW.c_str());

    std::wstring ytdlpPath = GetExeFolderW() + L"\\yt-dlp.exe";
    if (!FileExistsW(ytdlpPath)) {
        item->status = DownloadStatus::Failed;
        Engine_LogHistory(item->url, "FAILED (yt-dlp.exe not found)");
        return;
    }

    std::wstring args = L" --newline --no-warnings --no-playlist -f \"" +
                         Utf8ToWide(item->quality.format) + L"\"" +
                         L" -o \"" + folderW + L"%(title).100s.%(ext)s\" \"" +
                         Utf8ToWide(item->url) + L"\"";

    if (item->quality.is_audio_only) args += L" -x --audio-format mp3";
    else args += L" --merge-output-format mp4";

    if (!g_proxy.empty()) args += L" --proxy \"" + Utf8ToWide(g_proxy) + L"\"";

    std::wstring cmd = L"\"" + ytdlpPath + L"\"" + args;

    bool ok = RunProcessStreaming(cmd, item, [&](const std::string& line) {
        ParseYtDlpLine(item, line);
        size_t dpos = line.find("Destination: ");
        if (dpos != std::string::npos) {
            std::string full = line.substr(dpos + 13);
            item->full_path = full;
            size_t slash = full.find_last_of("\\/");
            item->display_name = (slash != std::string::npos) ? full.substr(slash + 1) : full;
        }
    });

    if (item->full_path.empty()) item->full_path = folderUtf8; // fallback: at least the folder

    if (item->cancel_requested.load()) {
        item->status = DownloadStatus::Cancelled;
        Engine_LogHistory(item->url, "CANCELLED");
        return;
    }

    if (ok) {
        item->progress = 100.0;
        item->status = DownloadStatus::Completed;
        Engine_LogHistory(item->url, "SUCCESS");
    } else {
        item->status = DownloadStatus::Failed;
        Engine_LogHistory(item->url, "FAILED");
    }
}

static void DownloadWorker(std::shared_ptr<DownloadItem> item) {
    if (item->is_direct) RunDirectDownload(item);
    else RunUniversalDownload(item);
}

// ============================================================
//  Public API
// ============================================================
void Engine_Init() {
    g_download_root = GetExeFolder() + "\\Downloads";
    g_history_path = GetExeFolder() + "\\download_history.txt";
    g_queue_path = GetExeFolder() + "\\download_queue.txt";

    _mkdir(g_download_root.c_str());
    for (const char* cat : {"Video", "Music", "Compressed", "Documents", "Other"}) {
        _mkdir((g_download_root + "\\" + cat).c_str());
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
}

std::shared_ptr<DownloadItem> Engine_AddDownload(const std::string& url, QualitySettings q, bool startImmediately) {
    auto item = std::make_shared<DownloadItem>();
    item->id = g_next_id++;
    item->url = url;
    item->quality = q;
    item->is_direct = IsDirectUrl(url);
    item->category = Engine_GuessCategory(url, q.is_audio_only);
    item->display_name = GetFilenameFromUrl(url);
    item->status = startImmediately ? DownloadStatus::Downloading : DownloadStatus::Queued;

    {
        std::lock_guard<std::mutex> lock(g_downloads_mutex);
        g_downloads.push_back(item);
    }

    if (startImmediately) {
        std::thread(DownloadWorker, item).detach();
    }
    return item;
}

void Engine_CancelDownload(const std::shared_ptr<DownloadItem>& item) {
    item->cancel_requested = true;
    std::lock_guard<std::mutex> lock(item->proc_mutex);
    if (item->hProcess) {
        TerminateProcess(item->hProcess, 1);
    }
}

void Engine_RemoveDownload(int id) {
    std::lock_guard<std::mutex> lock(g_downloads_mutex);
    g_downloads.erase(std::remove_if(g_downloads.begin(), g_downloads.end(),
        [id](const std::shared_ptr<DownloadItem>& it) { return it->id == id; }),
        g_downloads.end());
}

void Engine_RetryDownload(const std::shared_ptr<DownloadItem>& item) {
    item->cancel_requested = false;
    item->progress = 0.0;
    item->status = DownloadStatus::Downloading;
    std::thread(DownloadWorker, item).detach();
}

void Engine_StartAllQueued() {
    std::vector<std::shared_ptr<DownloadItem>> toStart;
    {
        std::lock_guard<std::mutex> lock(g_downloads_mutex);
        for (auto& item : g_downloads) {
            if (item->status == DownloadStatus::Queued) toStart.push_back(item);
        }
    }
    for (auto& item : toStart) {
        item->status = DownloadStatus::Downloading;
        std::thread(DownloadWorker, item).detach();
    }
}

// ---- MP3 conversion tool ----
bool Engine_ConvertToMp3(const std::wstring& inputPath, std::wstring& outPathOut, std::string& errorOut) {
    std::wstring ffmpegPath = GetExeFolderW() + L"\\ffmpeg.exe";
    if (!FileExistsW(ffmpegPath)) {
        errorOut = "ffmpeg.exe not found next to the application.";
        return false;
    }
    if (!FileExistsW(inputPath)) {
        errorOut = "That input file path doesn't exist. Double-check it (paste the exact path, including the extension).";
        return false;
    }

    std::wstring outputPath = inputPath + L".mp3";
    std::wstring cmd = L"\"" + ffmpegPath + L"\" -y -i \"" + inputPath +
                        L"\" -vn -acodec libmp3lame -q:a 2 \"" + outputPath + L"\"";

    std::vector<std::string> outputLines;
    bool ok = RunProcessStreaming(cmd, nullptr, [&](const std::string& line) {
        outputLines.push_back(line);
    });

    if (ok) {
        outPathOut = outputPath;
    } else {
        std::string tail;
        int start = (int)outputLines.size() - 6;
        if (start < 0) start = 0;
        for (int i = start; i < (int)outputLines.size(); i++) tail += outputLines[i] + " | ";
        if (tail.empty()) tail = "ffmpeg exited with an error but produced no output - check that ffmpeg.exe itself runs correctly.";
        errorOut = "ffmpeg conversion failed: " + tail;
    }
    return ok;
}

void Engine_InstallBrowserIntegration() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);

    RunProcessSimple(L"reg add HKCU\\Software\\Classes\\myidm /ve /d \"URL:Ultimate Downloader Protocol\" /f");
    RunProcessSimple(L"reg add HKCU\\Software\\Classes\\myidm /v \"URL Protocol\" /d \"\" /f");
    RunProcessSimple(L"reg add HKCU\\Software\\Classes\\myidm\\shell\\open\\command /ve /d \"\\\"" + path + L"\\\" \\\"%1\\\"\" /f");
}

void Engine_UpdateYtDlp() {
    std::wstring ytdlpPath = GetExeFolderW() + L"\\yt-dlp.exe";
    RunProcessSimple(L"\"" + ytdlpPath + L"\" --update-to nightly");
}

std::string Engine_RunSpeedTest() {
    CURL* curl = curl_easy_init();
    if (!curl) return "Speed test failed to start.";

    auto sinkCb = +[](void* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        return size * nmemb;
    };

    curl_easy_setopt(curl, CURLOPT_URL, "https://speed.cloudflare.com/__down?bytes=10000000");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sinkCb);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (!g_proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, g_proxy.c_str());

    auto start = GetTickCount64();
    CURLcode res = curl_easy_perform(curl);
    auto elapsedMs = GetTickCount64() - start;

    char result[128];
    if (res == CURLE_OK && elapsedMs > 0) {
        double mbps = (10.0 * 8.0) / (elapsedMs / 1000.0);
        snprintf(result, sizeof(result), "Approx. %.2f Mbps (10MB test file)", mbps);
    } else {
        snprintf(result, sizeof(result), "Speed test failed (check your connection/proxy).");
    }
    curl_easy_cleanup(curl);
    return std::string(result);
}

// ---- Clipboard monitor ----
static std::string GetClipboardTextUtf8() {
    if (!OpenClipboard(nullptr)) return "";
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == nullptr) { CloseClipboard(); return ""; }
    char* pszText = static_cast<char*>(GlobalLock(hData));
    std::string text = (pszText != nullptr) ? pszText : "";
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

void Engine_StartClipboardMonitor(QualitySettings q) {
    if (g_clipboard_thread_running.load()) return;
    g_clipboard_thread_running = true;
    g_clipboard_monitor_enabled = true;

    g_clipboard_thread = std::thread([q]() {
        std::string last;
        while (g_clipboard_thread_running.load()) {
            std::string c = GetClipboardTextUtf8();
            if (!c.empty() && c != last && c.rfind("http", 0) == 0) {
                last = c;
                Engine_AddDownload(c, q, true);
            }
            Sleep(600);
        }
    });
}

void Engine_StopClipboardMonitor() {
    g_clipboard_thread_running = false;
    g_clipboard_monitor_enabled = false;
    if (g_clipboard_thread.joinable()) g_clipboard_thread.join();
}

// ============================================================
//  Quality key mapping
// ============================================================
QualitySettings Engine_QualityFromKey(const std::string& key) {
    QualitySettings q;
    if (key == "1080p") q.format = "bestvideo[height<=1080]+bestaudio/best[height<=1080]";
    else if (key == "720p") q.format = "bestvideo[height<=720]+bestaudio/best[height<=720]";
    else if (key == "480p") q.format = "bestvideo[height<=480]+bestaudio/best[height<=480]";
    else if (key == "audio") { q.format = "bestaudio/best"; q.is_audio_only = true; }
    else q.format = "bestvideo+bestaudio/best";
    return q;
}

// ============================================================
//  Inbox watcher
// ============================================================
void Engine_StartInboxWatcher() {
    if (g_inbox_thread_running.load()) return;
    g_inbox_thread_running = true;

    g_inbox_thread = std::thread([]() {
        std::wstring inboxDir = Utf8ToWide(GetExeFolder() + "\\Inbox");
        CreateDirectoryW(inboxDir.c_str(), NULL);

        while (g_inbox_thread_running.load()) {
            std::error_code ec;
            if (std::filesystem::exists(inboxDir, ec)) {
                for (auto& entry : std::filesystem::directory_iterator(inboxDir, ec)) {
                    if (!entry.is_regular_file()) continue;
                    std::ifstream f(entry.path());
                    std::string url, qualityKey;
                    if (std::getline(f, url)) {
                        std::getline(f, qualityKey);
                        f.close();
                        while (!url.empty() && (url.back() == '\r' || url.back() == '\n')) url.pop_back();
                        while (!qualityKey.empty() && (qualityKey.back() == '\r' || qualityKey.back() == '\n')) qualityKey.pop_back();
                        if (!url.empty()) {
                            Engine_AddDownload(url, Engine_QualityFromKey(qualityKey), true);
                        }
                        std::filesystem::remove(entry.path(), ec);
                    }
                }
            }
            Sleep(700);
        }
    });
}

void Engine_StopInboxWatcher() {
    g_inbox_thread_running = false;
    if (g_inbox_thread.joinable()) g_inbox_thread.join();
}

// ============================================================
//  Native messaging host installer
// ============================================================
bool Engine_InstallNativeMessagingHost(const std::string& extensionId, std::string& resultMsg) {
    if (extensionId.empty()) {
        resultMsg = "Please paste the extension ID first (see chrome://extensions after loading it unpacked).";
        return false;
    }

    std::wstring hostExe = GetExeFolderW() + L"\\UltimateDownloaderHost.exe";
    if (!FileExistsW(hostExe)) {
        resultMsg = "UltimateDownloaderHost.exe not found next to this app. Build it first (see README).";
        return false;
    }

    std::string hostExeUtf8 = WideToUtf8(hostExe);
    std::string escapedPath;
    for (char c : hostExeUtf8) {
        if (c == '\\') escapedPath += "\\\\";
        else escapedPath += c;
    }

    std::string manifestJson =
        "{\n"
        "  \"name\": \"com.ultimatedownloader.native\",\n"
        "  \"description\": \"Ultimate Downloader Native Messaging Host\",\n"
        "  \"path\": \"" + escapedPath + "\",\n"
        "  \"type\": \"stdio\",\n"
        "  \"allowed_origins\": [\"chrome-extension://" + extensionId + "/\"]\n"
        "}\n";

    std::string manifestPath = GetExeFolder() + "\\native_host_manifest.json";
    std::ofstream f(manifestPath, std::ios::binary);
    if (!f.is_open()) {
        resultMsg = "Could not write native_host_manifest.json";
        return false;
    }
    f << manifestJson;
    f.close();

    std::wstring manifestPathW = Utf8ToWide(manifestPath);
    bool ok1 = RunProcessSimple(L"reg add \"HKCU\\Software\\Google\\Chrome\\NativeMessagingHosts\\com.ultimatedownloader.native\" /ve /d \"" + manifestPathW + L"\" /f");
    bool ok2 = RunProcessSimple(L"reg add \"HKCU\\Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.ultimatedownloader.native\" /ve /d \"" + manifestPathW + L"\" /f");

    if (ok1 || ok2) {
        resultMsg = "Native messaging host registered for " +
                    std::string(ok1 ? "Chrome" : "") + std::string(ok1 && ok2 ? " + " : "") +
                    std::string(ok2 ? "Edge" : "") + ". Reload the extension and try it.";
        return true;
    }
    resultMsg = "Registry write failed.";
    return false;
}
