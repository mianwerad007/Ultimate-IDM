#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>

// ============================================================
//  Ultimate Downloader - Engine Layer
// ============================================================

enum class DownloadStatus {
    Queued,
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled
};

struct QualitySettings {
    std::string format;       // yt-dlp -f format string
    bool is_audio_only = false;
};

struct DownloadItem {
    int id = 0;
    std::string url;
    std::string display_name;   // filename / title shown in the UI
    std::string category;       // "Video", "Music", "Compressed", "Documents", "Other"
    std::string size_str = "-";
    std::string speed_str = "-";
    std::string eta_str = "-";
    std::string full_path;      // full on-disk path once known (used by "Open Folder")

    std::atomic<double> progress{0.0};        // 0..100
    std::atomic<DownloadStatus> status{DownloadStatus::Queued};
    std::atomic<bool> cancel_requested{false};

    std::atomic<long long> last_bytes{0};
    std::atomic<long long> last_time_ms{0};

    std::mutex proc_mutex;
    HANDLE hProcess = nullptr;

    QualitySettings quality;
    bool is_direct = false;
};

// ---- Global engine state ----
extern std::vector<std::shared_ptr<DownloadItem>> g_downloads;
extern std::mutex g_downloads_mutex;
extern std::string g_proxy;
extern std::string g_download_root;
extern std::string g_history_path;
extern std::string g_queue_path;
extern bool g_clipboard_monitor_enabled;

// ---- Lifecycle ----
void Engine_Init();

// ---- Core actions ----
std::shared_ptr<DownloadItem> Engine_AddDownload(const std::string& url, QualitySettings q, bool startImmediately);
void Engine_CancelDownload(const std::shared_ptr<DownloadItem>& item);
void Engine_RemoveDownload(int id);
void Engine_RetryDownload(const std::shared_ptr<DownloadItem>& item);

// ---- Batch / queue ----
void Engine_SaveQueueToDisk();
void Engine_LoadQueueFromDisk(QualitySettings q);
void Engine_StartAllQueued();

// ---- Tools ----
bool Engine_ConvertToMp3(const std::wstring& inputPath, std::wstring& outPathOut, std::string& errorOut);
void Engine_InstallBrowserIntegration();
void Engine_UpdateYtDlp();
std::string Engine_RunSpeedTest();

// ---- History ----
void Engine_LogHistory(const std::string& url, const std::string& status);
std::vector<std::string> Engine_ReadHistoryLines();
void Engine_ClearHistory();

// ---- Clipboard monitor ----
void Engine_StartClipboardMonitor(QualitySettings q);
void Engine_StopClipboardMonitor();

// ---- Inbox watcher (browser extension native messaging) ----
void Engine_StartInboxWatcher();
void Engine_StopInboxWatcher();
QualitySettings Engine_QualityFromKey(const std::string& key);

// ---- Native messaging host installation (Chrome/Edge) ----
bool Engine_InstallNativeMessagingHost(const std::string& extensionId, std::string& resultMsg);

// ---- Helpers exposed for the UI ----
std::string Engine_GuessCategory(const std::string& url, bool audioOnly);
const char* Engine_StatusToString(DownloadStatus s);
