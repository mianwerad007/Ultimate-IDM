// ============================================================
//  Ultimate Downloader - Native Messaging Host
//  Launched by Chrome/Edge, talks over stdin/stdout using Chrome's
//  native messaging framing: [4-byte little-endian length][UTF-8 JSON]
//  Drops the received URL into the GUI app's "Inbox" folder and
//  wakes/launches the GUI.
// ============================================================
#include <windows.h>
#include <string>
#include <fstream>
#include <cstdint>

static std::wstring GetExeFolderW() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    return path.substr(0, path.find_last_of(L"\\/"));
}

static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";
    size_t i = pos + 1;
    std::string result;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            result += json[i + 1];
            i += 2;
        } else {
            result += json[i];
            i += 1;
        }
    }
    return result;
}

static bool ReadExact(HANDLE h, void* buf, DWORD count) {
    DWORD total = 0;
    while (total < count) {
        DWORD got = 0;
        if (!ReadFile(h, (BYTE*)buf + total, count - total, &got, NULL) || got == 0) return false;
        total += got;
    }
    return true;
}

static void WriteNativeMessage(HANDLE hOut, const std::string& json) {
    uint32_t len = (uint32_t)json.size();
    DWORD written;
    WriteFile(hOut, &len, 4, &written, NULL);
    WriteFile(hOut, json.data(), len, &written, NULL);
}

static void DropInInbox(const std::string& url, const std::string& quality) {
    std::wstring inboxDir = GetExeFolderW() + L"\\Inbox";
    CreateDirectoryW(inboxDir.c_str(), NULL);

    wchar_t name[64];
    swprintf_s(name, L"\\%llu_%u.url", (unsigned long long)GetTickCount64(), GetCurrentProcessId());
    std::wstring path = inboxDir + name;

    std::ofstream f(path, std::ios::binary);
    if (f.is_open()) {
        f << url << "\n" << quality << "\n";
    }
}

static void WakeOrLaunchGui() {
    HWND hwnd = FindWindowW(L"UltimateDownloaderGUI", NULL);
    if (hwnd) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return;
    }
    std::wstring exeDir = GetExeFolderW();
    std::wstring guiPath = exeDir + L"\\UltimateDownloaderGUI.exe";
    ShellExecuteW(NULL, L"open", guiPath.c_str(), NULL, exeDir.c_str(), SW_SHOWNORMAL);
}

int wmain() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    for (;;) {
        uint32_t len = 0;
        if (!ReadExact(hIn, &len, 4)) break;
        if (len == 0 || len > 10 * 1024 * 1024) break;

        std::string payload(len, '\0');
        if (!ReadExact(hIn, &payload[0], len)) break;

        std::string url = ExtractJsonString(payload, "url");
        std::string quality = ExtractJsonString(payload, "quality");
        if (quality.empty()) quality = "best";

        if (!url.empty()) {
            DropInInbox(url, quality);
            WakeOrLaunchGui();
            WriteNativeMessage(hOut, "{\"status\":\"ok\"}");
        } else {
            WriteNativeMessage(hOut, "{\"status\":\"error\",\"message\":\"missing url\"}");
        }
    }
    return 0;
}
