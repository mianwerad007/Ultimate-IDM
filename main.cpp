#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdio>
#include <windows.h>
#include <shellapi.h>
#include <curl/curl.h>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <direct.h>
#include <sys/stat.h>
#include <ctime>
#include <io.h>
#include <fcntl.h>
#include <limits>

// --- CONFIGURATION ---
const int NUM_THREADS = 4;
std::atomic<long> total_bytes_downloaded{0};
std::atomic<long> total_file_size{0};
std::mutex log_mutex;
std::string GLOBAL_QUEUE_PATH = "";
std::string GLOBAL_HISTORY_PATH = "";
std::string GLOBAL_PROXY = "";

// --- COLORS ---
const std::string RESET = "\033[0m";
const std::string GREEN = "\033[1;32m";
const std::string CYAN  = "\033[1;36m";
const std::string YELLOW = "\033[1;33m";
const std::string RED   = "\033[1;31m";
const std::string MAGENTA = "\033[1;35m";

// --- UNICODE UTILITIES ---
std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Converts Wide String (std::wstring) to UTF-8 (std::string)
std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring get_exe_folder_w() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    return path.substr(0, path.find_last_of(L"\\/"));
}

std::string get_exe_folder() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    return path.substr(0, path.find_last_of("\\/"));
}

// Native Windows process execution to bypass CMD.exe encoding issues
bool ExecuteProcessW(const std::wstring& cmd) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Mutable buffer for CreateProcessW
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    return false;
}

std::string get_timestamp() {
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    return std::string(buf);
}

void log_history(std::string url, std::string status) {
    std::ofstream file;
    file.open(GLOBAL_HISTORY_PATH, std::ios_base::app);
    if (file.is_open()) {
        file << "[" << get_timestamp() << "] " << status << ": " << url << "\n";
        file.close();
    }
}

bool file_exists_w(const std::wstring &name) {
    struct _stat64i32 buffer;
    return (_wstat(name.c_str(), &buffer) == 0);
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

void clean_print(std::string msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << msg << std::flush;
}

std::string get_filename_from_url(std::string url) {
    size_t last_slash = url.find_last_of("/");
    if (last_slash == std::string::npos) return "file.dat";
    std::string name = url.substr(last_slash + 1);
    size_t param_start = name.find("?");
    if (param_start != std::string::npos) name = name.substr(0, param_start);
    return name;
}

std::string get_clipboard_text() {
    if (!OpenClipboard(nullptr)) return "";
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == nullptr) {
        CloseClipboard();
        return "";
    }
    char *pszText = static_cast<char *>(GlobalLock(hData));
    std::string text = (pszText != nullptr) ? pszText : "";
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

void add_to_queue(std::string url) {
    std::ofstream file;
    file.open(GLOBAL_QUEUE_PATH, std::ios_base::app);
    if (file.is_open()) {
        file << url << "\n";
        file.close();
        clean_print("\n   " + GREEN + "[QUEUED]" + RESET + " Added to Batch List.\n");
    } else {
        clean_print("\n   " + RED + "[ERROR]" + RESET + " Could not write to queue file.\n");
    }
}

void show_completion_menu(std::wstring filePath, std::wstring folderPath) {
    std::cout << "\n   " << CYAN << "--- DOWNLOAD COMPLETE ---" << RESET << "\n";
    std::cout << "   [1] Open File\n";
    std::cout << "   [2] Open Folder\n";
    std::cout << "   [3] Return/Close\n";
    std::cout << "   Choice: ";
    int choice;
    if (!(std::wcin >> choice)) {
        std::wcin.clear();
        std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');
        return;
    }
    std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');

    if (choice == 1) {
        ShellExecuteW(NULL, L"open", filePath.c_str(), NULL, NULL, SW_SHOW);
    } else if (choice == 2) {
        ShellExecuteW(NULL, L"explore", folderPath.c_str(), NULL, NULL, SW_SHOW);
    }
}

void install_integration() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    
    std::wstring cmd1 = L"reg add HKCR\\myidm /ve /d \"URL:Ultimate Downloader Protocol\" /f";
    std::wstring cmd2 = L"reg add HKCR\\myidm /v \"URL Protocol\" /d \"\" /f";
    std::wstring cmd3 = L"reg add HKCR\\myidm\\shell\\open\\command /ve /d \"\\\"" + path + L"\\\" \\\"%1\\\"\" /f";

    std::cout << "\n   " << YELLOW << "INSTALLING INTEGRATION..." << RESET << "\n";
    ExecuteProcessW(cmd1);
    ExecuteProcessW(cmd2);
    ExecuteProcessW(cmd3);

    std::cout << "   " << GREEN << "[DONE]" << RESET << " Protocol 'myidm://' registered!\n";
    std::cout << "   Press Enter...";
    std::wcin.get();
}

void set_proxy() {
    std::cout << "\n   " << MAGENTA << "--- PROXY CONFIGURATION ---" << RESET << "\n";
    std::cout << "   Current Proxy: " << (GLOBAL_PROXY.empty() ? "None" : GLOBAL_PROXY) << "\n";
    std::cout << "   Enter Proxy (e.g. http://1.2.3.4:8080) or 'clear': ";
    std::string input;
    std::cin >> input;
    if (input == "clear") {
        GLOBAL_PROXY = "";
        std::cout << "   Proxy Disabled.\n";
    } else {
        GLOBAL_PROXY = input;
        std::cout << "   Proxy Set!\n";
    }
    Sleep(1500);
}

void convert_mp3() {
    std::cout << "\n   " << YELLOW << "DROP VIDEO FILE HERE: " << RESET;
    
    // Switch to Wide Input to capture Unicode characters from the console
    std::wstring input_path;
    // Clear buffer properly before reading
    std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');
    std::getline(std::wcin, input_path);
    
    // Robust trimming for Wide Strings (removes drag-and-drop quotes and spaces)
    size_t first = input_path.find_first_not_of(L" \t\n\r\"");
    size_t last = input_path.find_last_not_of(L" \t\n\r\"");
    if (first != std::wstring::npos && last != std::wstring::npos) {
        input_path = input_path.substr(first, (last - first + 1));
    }

    if (input_path.length() < 3) return;
    
    std::wstring output_path = input_path + L".mp3";
    std::cout << "   Converting... Please wait.\n";
    
    std::wstring ffmpeg_path = get_exe_folder_w() + L"\\ffmpeg.exe";
    if (!file_exists_w(ffmpeg_path)) {
        std::cout << "\n   " << RED << "[ERROR] ffmpeg.exe not found!" << RESET << "\n";
        return;
    }

    // Build Native Command for CreateProcess to bypass CMD encoding issues
    std::wstring cmd = L"\"" + ffmpeg_path + L"\" -i \"" + input_path + L"\" -vn -acodec libmp3lame -q:a 2 \"" + output_path + L"\"";

    bool success = ExecuteProcessW(cmd);

    if (success) {
        std::cout << "\n   " << GREEN << "[SUCCESS]" << RESET << " Audio saved as MP3!\n";
        MessageBoxW(NULL, L"Audio Conversion Successful!", L"Ultimate Downloader", MB_OK | MB_ICONINFORMATION);
        
        size_t last_slash = output_path.find_last_of(L"\\/");
        std::wstring folder = (last_slash != std::wstring::npos) ? output_path.substr(0, last_slash) : L".";
        show_completion_menu(output_path, folder);
    } else {
        std::cout << "\n   " << RED << "[FAILED]" << RESET << " Conversion Error (Ensure ffmpeg is valid)." << RESET << "\n";
    }
    std::cout << "   Press Enter to return...";
    std::wcin.get();
}

struct QualitySettings {
    std::string format;
    std::string extra_flags;
    bool is_audio_only;
};

QualitySettings ask_quality() {
    std::cout << "\n   " << CYAN << "SELECT QUALITY:" << RESET << "\n";
    std::cout << "   [1] Best (4K/2K/Highest Available)\n";
    std::cout << "   [2] 1080p (Full HD)\n";
    std::cout << "   [3] 720p (HD)\n";
    std::cout << "   [4] 480p (SD)\n";
    std::cout << "   [5] Audio Only (MP3)\n";
    std::cout << "   Choice: ";
    int choice;
    if (!(std::wcin >> choice)) {
        std::wcin.clear();
        std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');
        choice = 1;
    }
    std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');

    QualitySettings s;
    s.extra_flags = "";
    s.is_audio_only = false;
    
    if (choice == 1) s.format = "bestvideo+bestaudio/best";
    else if (choice == 2) s.format = "bestvideo[height<=1080]+bestaudio/best[height<=1080]";
    else if (choice == 3) s.format = "bestvideo[height<=720]+bestaudio/best[height<=720]";
    else if (choice == 4) s.format = "bestvideo[height<=480]+bestaudio/best[height<=480]";
    else if (choice == 5) {
        s.format = "bestaudio/best";
        s.extra_flags = "-x --audio-format mp3 ";
        s.is_audio_only = true;
    }
    else s.format = "bestvideo+bestaudio/best";
    
    return s;
}

void run_direct(std::string url, bool background) {
    std::string filename = get_filename_from_url(url);
    std::wstring folder = get_exe_folder_w() + L"\\Downloads";
    std::wstring fullPath = folder + L"\\" + utf8_to_wstring(filename);

    if (background) clean_print("\n   " + YELLOW + "[STARTED]" + RESET + " Direct Download: " + filename + "\n");
    else std::cout << "\n" << MAGENTA << "   [DIRECT ENGINE] " << RESET << "Targeting File Server...\n";
    
    std::wstring curl_cmd = L"curl -L -o \"Downloads/" + utf8_to_wstring(filename) + L"\" \"" + utf8_to_wstring(url) + L"\"";
    if (!GLOBAL_PROXY.empty()) curl_cmd += L" --proxy \"" + utf8_to_wstring(GLOBAL_PROXY) + L"\"";
    
    bool res = ExecuteProcessW(curl_cmd);
    if (res) {
        log_history(url, "SUCCESS");
        if (background) clean_print("\n   " + GREEN + "[DONE]" + RESET + " " + filename + "\n");
        MessageBoxW(NULL, utf8_to_wstring(filename + " Downloaded Successfully!").c_str(), L"Ultimate Downloader", MB_OK | MB_ICONINFORMATION);
        if (!background) show_completion_menu(fullPath, folder);
    } else {
        log_history(url, "FAILED");
    }
}

void run_universal(std::string url, bool background, QualitySettings quality) {
    if (background) clean_print("\n   " + YELLOW + "[STARTED]" + RESET + " " + url.substr(0, 30) + "...\n");
    else std::cout << "\n" << YELLOW << "   [UNIVERSAL ENGINE] " << RESET << "Analyzing...\n";

    std::string folder = "Downloads/Video/";
    if (quality.is_audio_only) folder = "Downloads/Music/";
    else if (to_lower(url).find("tiktok") != std::string::npos) folder = "Downloads/TikTok/";
    
    std::wstring full_folder = get_exe_folder_w() + L"\\" + utf8_to_wstring(folder);
    std::replace(full_folder.begin(), full_folder.end(), '/', '\\');

    _wmkdir(full_folder.c_str());

    std::wstring ytdlp_path = get_exe_folder_w() + L"\\yt-dlp.exe";
    if (!file_exists_w(ytdlp_path)) {
        std::cout << "\n   " << RED << "[CRITICAL ERROR] yt-dlp.exe NOT FOUND!" << RESET << "\n";
        return;
    }

    std::wstring args = L" --no-warnings --no-playlist -o \"" + full_folder + L"%(title).100s.%(ext)s\" \"" + utf8_to_wstring(url) + L"\"";
    if (!quality.is_audio_only) args += L" --merge-output-format mp4";
    else args += L" -x --audio-format mp3";
    
    if (!GLOBAL_PROXY.empty()) args += L" --proxy \"" + utf8_to_wstring(GLOBAL_PROXY) + L"\"";

    std::wstring cmd = L"\"" + ytdlp_path + L"\" " + args;

    bool result = ExecuteProcessW(cmd);
    if (result) {
        clean_print("\n   " + GREEN + "[SUCCESS]" + RESET + " Saved to: " + folder + "\n");
        log_history(url, "SUCCESS");
        MessageBoxW(NULL, L"Download Task Completed!", L"Ultimate Downloader", MB_OK | MB_ICONINFORMATION);
        if (!background) show_completion_menu(L"", full_folder);
    } else {
        clean_print("\n   " + RED + "[ERROR] Download failed." + RESET + "\n");
        log_history(url, "FAILED");
    }
}

void process_link(std::string url, bool background, QualitySettings quality) {
    std::string lower = to_lower(url);
    if (lower.find(".zip") != std::string::npos || lower.find(".exe") != std::string::npos) run_direct(url, background);
    else run_universal(url, background, quality);
}

void process_queue_parallel() {
    std::ifstream file(GLOBAL_QUEUE_PATH);
    if (file.peek() == std::ifstream::traits_type::eof()) {
        std::cout << "\n   " << RED << "[EMPTY]" << RESET << " Queue is empty.\n";
        return;
    }
    std::cout << "\n   " << YELLOW << "OPTIONS:" << RESET << "\n";
    std::cout << "   Shutdown PC after download finishes? (y/n): ";
    wchar_t sd_choice;
    std::wcin >> sd_choice;
    QualitySettings batch_quality = ask_quality();
    std::vector<std::string> links;
    std::string url;
    while (std::getline(file, url)) if (url.length() > 5) links.push_back(url);
    file.close();

    std::cout << "\n   " << MAGENTA << "LAUNCHING ENGINES..." << RESET << "\n";
    std::vector<std::thread> pool;
    for (const auto &l : links) pool.push_back(std::thread(process_link, l, true, batch_quality));
    for (auto &t : pool) if (t.joinable()) t.join();
    
    std::ofstream clr(GLOBAL_QUEUE_PATH, std::ofstream::out | std::ofstream::trunc);
    clr.close();
    std::cout << "\n   " << GREEN << "ALL TASKS COMPLETED." << RESET << "\n";
    MessageBoxW(NULL, L"Batch Download Completed!", L"Ultimate Downloader", MB_OK | MB_ICONINFORMATION);
    
    if (sd_choice == L'y' || sd_choice == L'Y') {
        ExecuteProcessW(L"shutdown /s /t 60");
    }
    std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');
    std::cout << "   Press any key to return to menu...";
    std::wcin.get();
}

void show_tools_menu() {
    while (true) {
        system("cls");
        std::cout << "\n" << CYAN << "   ========================================\n";
        std::cout << "               EXTRA TOOLS   \n";
        std::cout << "   ========================================\n" << RESET;
        std::cout << "   [1] Internet Speed Test\n";
        std::cout << "   [2] Clean Temporary Files\n";
        std::cout << "   [3] Convert Video to MP3\n";
        std::cout << "   [4] View Download History\n";
        std::cout << "   [5] Configure Proxy\n";
        std::cout << "   [6] Back to Main Menu\n";
        std::cout << "\n   Select Tool: ";
        int tool;
        if (!(std::wcin >> tool)) {
            std::wcin.clear(); std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n'); continue;
        }
        if (tool == 1) {
            std::cout << "\n   Running Speed Test...\n";
            ExecuteProcessW(L"curl -o nul https://speed.cloudflare.com/__down?bytes=10000000");
            std::cout << "   Speed Test Finished.\n";
            std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n'); std::wcin.get();
        }
        else if (tool == 2) {
            ExecuteProcessW(L"cmd /c del *.tmp");
            ExecuteProcessW(L"cmd /c del *.part");
            std::cout << "\n   Cleaned!";
            Sleep(1000);
        }
        else if (tool == 3) convert_mp3();
        else if (tool == 4) {
            std::wstring cmd = L"cmd /c type \"" + utf8_to_wstring(GLOBAL_HISTORY_PATH) + L"\"";
            ExecuteProcessW(cmd);
            std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n'); std::wcin.get();
        }
        else if (tool == 5) set_proxy();
        else break;
    }
}

int main(int argc, char *argv[]) {
    // Crucial for capturing Wide Unicode input from the user
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stdin), _O_WTEXT);
    
    // We must reset to standard mode for colored text to work, but we'll use wcin for inputs
    _setmode(_fileno(stdout), _O_TEXT);

    GLOBAL_QUEUE_PATH = get_exe_folder() + "\\download_queue.txt";
    GLOBAL_HISTORY_PATH = get_exe_folder() + "\\download_history.txt";
    system("color");
    
    _wmkdir(utf8_to_wstring(get_exe_folder() + "\\Downloads\\Video").c_str());
    _wmkdir(utf8_to_wstring(get_exe_folder() + "\\Downloads\\Music").c_str());

    if (argc > 1) {
        std::string url = argv[1];
        if (url.find("myidm://") == 0) {
            url = url.substr(8);
            if (!url.empty() && url.back() == '/') url.pop_back();
            if (url.find("https//") == 0) url.replace(0, 7, "https://");
            add_to_queue(url);
            Sleep(2000);
            return 0;
        }
        int q_id = (argc > 2) ? std::atoi(argv[2]) : 1;
        QualitySettings q;
        if (q_id == 5) {
            q.format = "bestaudio/best"; q.extra_flags = "-x --audio-format mp3 "; q.is_audio_only = true;
        } else q.format = "bestvideo+bestaudio/best";
        process_link(url, false, q);
        return 0;
    }

    while (true) {
        system("cls");
        std::cout << "\n" << CYAN << "   ========================================\n";
        std::cout << "        ULTIMATE DOWNLOADER (V13 GOLD)   \n";
        std::cout << "   ========================================\n" << RESET;
        std::cout << "   [1] Auto-Monitor Clipboard\n";
        std::cout << "   [2] Paste Link Manually\n";
        std::cout << "   [3] Start Parallel Queue\n";
        std::cout << "   [4] Install Browser Integration\n";
        std::cout << "   [5] Update Core Engines\n";
        std::cout << "   [6] Extra Tools\n";
        std::cout << "   [7] Exit\n";
        std::cout << "\n   Select Mode: ";
        int mode;
        if (!(std::wcin >> mode)) { 
            std::wcin.clear(); 
            std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n'); 
            continue; 
        }
        std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');

        if (mode == 1) {
            QualitySettings q = ask_quality();
            std::string last = "";
            while (true) {
                std::string c = get_clipboard_text();
                if (c != last && c.find("http") == 0) {
                    last = c; process_link(c, false, q);
                }
                Sleep(500);
            }
        }
        else if (mode == 2) {
            std::cout << "\n   Paste Link: ";
            
            // Clean Wide input
            std::wstring wlink;
            std::getline(std::wcin, wlink);
            
            std::string l = wstring_to_utf8(wlink);
            
            if (l.length() > 0) {
                std::cout << "   [1] Download Now [2] Add to Queue: ";
                int c; 
                if (!(std::wcin >> c)) {
                    std::wcin.clear();
                    std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');
                    c = 1;
                }
                std::wcin.ignore((std::numeric_limits<std::streamsize>::max)(), L'\n');
                
                if (c == 2) add_to_queue(l);
                else process_link(l, false, ask_quality());
            }
        }
        else if (mode == 3) process_queue_parallel();
        else if (mode == 4) install_integration();
        else if (mode == 5) ExecuteProcessW(L"yt-dlp.exe --update-to nightly");
        else if (mode == 6) show_tools_menu();
        else if (mode == 7) break;
    }
    return 0;
}