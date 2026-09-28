// ============================================================
//  Ultimate Downloader - GUI Edition
//  Win32 + DirectX11 + Dear ImGui
// ============================================================
#define NOMINMAX
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>

#include "DownloadEngine.h"
#include "resource.h"

// ---- DirectX globals ----
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static HWND                     g_hwnd = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const wchar_t* kWindowClassName = L"UltimateDownloaderGUI";
static const wchar_t* kMutexName = L"UltimateDownloaderGUI_SingleInstance";

// ============================================================
//  App state
// ============================================================
static std::string g_selectedCategory = "All Downloads";
static char g_urlInputBuf[2048] = "";
static int  g_qualityChoiceIdx = 0;
static bool g_showAddUrlModal = false;
static bool g_showOptionsModal = false;
static bool g_showHistoryModal = false;
static bool g_showSchedulerModal = false;
static bool g_showMp3Modal = false;
static char g_mp3PathBuf[1024] = "";
static std::string g_mp3StatusMsg;
static char g_proxyInputBuf[256] = "";
static char g_extensionIdBuf[128] = "";
static std::string g_nmhResultMsg;
static bool g_nmhResultOk = false;
static std::string g_toastMessage;
static double g_toastTimer = 0.0;

static void ShowToast(const std::string& msg) {
    g_toastMessage = msg;
    g_toastTimer = 3.0;
}

// ---- Color palette for semantic action buttons / status pills ----
static const ImVec4 COL_BLUE   = ImVec4(0.25f, 0.50f, 0.95f, 1.0f);
static const ImVec4 COL_GREEN  = ImVec4(0.20f, 0.72f, 0.42f, 1.0f);
static const ImVec4 COL_RED    = ImVec4(0.92f, 0.32f, 0.32f, 1.0f);
static const ImVec4 COL_ORANGE = ImVec4(0.96f, 0.64f, 0.20f, 1.0f);
static const ImVec4 COL_GRAY   = ImVec4(0.32f, 0.35f, 0.42f, 1.0f);
static const ImVec4 COL_PURPLE = ImVec4(0.55f, 0.40f, 0.95f, 1.0f);

static ImVec4 Lighten(ImVec4 c, float amt) {
    return ImVec4(std::min(c.x + amt, 1.0f), std::min(c.y + amt, 1.0f), std::min(c.z + amt, 1.0f), c.w);
}
static ImVec4 Darken(ImVec4 c, float amt) {
    return ImVec4(std::max(c.x - amt, 0.0f), std::max(c.y - amt, 0.0f), std::max(c.z - amt, 0.0f), c.w);
}

static bool ColoredButton(const char* label, ImVec4 color, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lighten(color, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Darken(color, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

static bool ColoredSmallButton(const char* label, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lighten(color, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Darken(color, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool clicked = ImGui::SmallButton(label);
    ImGui::PopStyleColor(4);
    return clicked;
}

static std::string WideToUtf8Local(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &out[0], len, NULL, NULL);
    return out;
}

static std::wstring Utf8ToWideLocal(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring out(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], len);
    return out;
}

static QualitySettings QualityFromChoice(int idx) {
    QualitySettings q;
    switch (idx) {
        case 0: q.format = "bestvideo+bestaudio/best"; break;
        case 1: q.format = "bestvideo[height<=1080]+bestaudio/best[height<=1080]"; break;
        case 2: q.format = "bestvideo[height<=720]+bestaudio/best[height<=720]"; break;
        case 3: q.format = "bestvideo[height<=480]+bestaudio/best[height<=480]"; break;
        case 4: q.format = "bestaudio/best"; q.is_audio_only = true; break;
        default: q.format = "bestvideo+bestaudio/best";
    }
    return q;
}

// ============================================================
//  Theme
// ============================================================
static void ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 8.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 7.0f;
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 10);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4 bg        = ImVec4(0.071f, 0.078f, 0.106f, 1.00f);
    ImVec4 bgLight   = ImVec4(0.106f, 0.114f, 0.153f, 1.00f);
    ImVec4 bgLighter = ImVec4(0.153f, 0.165f, 0.212f, 1.00f);
    ImVec4 accent    = ImVec4(0.25f, 0.50f, 0.95f, 1.00f);
    ImVec4 accentHov = ImVec4(0.34f, 0.58f, 1.00f, 1.00f);
    ImVec4 text      = ImVec4(0.92f, 0.93f, 0.96f, 1.00f);
    ImVec4 textDim   = ImVec4(0.52f, 0.55f, 0.63f, 1.00f);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textDim;
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = bgLight;
    colors[ImGuiCol_PopupBg] = ImVec4(0.086f, 0.094f, 0.125f, 0.99f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.29f, 0.6f);
    colors[ImGuiCol_FrameBg] = bgLighter;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.30f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.27f, 0.35f, 1.0f);
    colors[ImGuiCol_TitleBg] = bgLight;
    colors[ImGuiCol_TitleBgActive] = bgLight;
    colors[ImGuiCol_MenuBarBg] = bgLight;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0,0,0,0);
    colors[ImGuiCol_ScrollbarGrab] = bgLighter;
    colors[ImGuiCol_ScrollbarGrabHovered] = accent;
    colors[ImGuiCol_ScrollbarGrabActive] = accentHov;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHov;
    colors[ImGuiCol_Button] = bgLighter;
    colors[ImGuiCol_ButtonHovered] = accent;
    colors[ImGuiCol_ButtonActive] = accentHov;
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.26f, 0.34f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.21f, 0.28f, 1.0f);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.31f, 0.5f);
    colors[ImGuiCol_Tab] = bgLight;
    colors[ImGuiCol_TabHovered] = accentHov;
    colors[ImGuiCol_TabActive] = accent;
    colors[ImGuiCol_TabUnfocused] = bgLight;
    colors[ImGuiCol_TabUnfocusedActive] = bgLighter;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_TableHeaderBg] = bgLight;
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.20f, 0.22f, 0.29f, 0.6f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.16f, 0.18f, 0.24f, 0.4f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
}

// ============================================================
//  UI pieces
// ============================================================
struct CategoryCount { std::string name; int count; };

static std::vector<CategoryCount> ComputeCategoryCounts() {
    std::vector<CategoryCount> result = {
        {"All Downloads", 0}, {"Video", 0}, {"Music", 0}, {"Compressed", 0}, {"Documents", 0}, {"Other", 0}
    };
    std::lock_guard<std::mutex> lock(g_downloads_mutex);
    for (auto& item : g_downloads) {
        result[0].count++;
        for (auto& c : result) if (c.name == item->category) { c.count++; break; }
    }
    return result;
}

static void DrawSidebar() {
    ImGui::BeginChild("Sidebar", ImVec2(210, 0), true);

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetWindowFontScale(1.15f);
    ImGui::TextColored(COL_BLUE, "  Ultimate Downloader");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    auto counts = ComputeCategoryCounts();
    const char* icons[] = { "* ", "V ", "M ", "Z ", "D ", "? " };
    const ImVec4 catColors[] = { COL_BLUE, COL_PURPLE, COL_GREEN, COL_ORANGE, COL_GRAY, COL_GRAY };

    for (size_t i = 0; i < counts.size(); i++) {
        bool selected = (g_selectedCategory == counts[i].name);
        ImVec2 rowPos = ImGui::GetCursorScreenPos();
        float rowWidth = ImGui::GetContentRegionAvail().x;

        std::string label = "  " + std::string(icons[i]) + "  " + counts[i].name + "  (" + std::to_string(counts[i].count) + ")";
        if (ImGui::Selectable(("##cat" + std::to_string(i)).c_str(), selected, 0, ImVec2(rowWidth, 30))) {
            g_selectedCategory = counts[i].name;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (selected) {
            dl->AddRectFilled(rowPos, ImVec2(rowPos.x + 3, rowPos.y + 30), ImGui::GetColorU32(catColors[i]), 2.0f);
        }
        ImVec2 textPos = ImVec2(rowPos.x + 8, rowPos.y + 7);
        dl->AddText(textPos, ImGui::GetColorU32(selected ? ImVec4(1,1,1,1) : ImVec4(0.82f,0.84f,0.90f,1.0f)), label.c_str());
    }

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Separator();
    ImGui::TextDisabled("  QUEUE");
    ImGui::Spacing();
    if (ColoredButton("Start Queue", COL_GREEN, ImVec2(-1, 32))) {
        Engine_StartAllQueued();
        ShowToast("Started all queued downloads");
    }
    if (ImGui::Button("Save Queue", ImVec2(-1, 30))) {
        Engine_SaveQueueToDisk();
        ShowToast("Queue saved to disk");
    }

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Separator();
    ImGui::TextDisabled("  MONITOR");
    ImGui::Spacing();
    bool monitorOn = g_clipboard_monitor_enabled;
    if (ImGui::Checkbox("Auto-monitor clipboard", &monitorOn)) {
        if (monitorOn) {
            Engine_StartClipboardMonitor(QualityFromChoice(g_qualityChoiceIdx));
            ShowToast("Clipboard monitor enabled");
        } else {
            Engine_StopClipboardMonitor();
            ShowToast("Clipboard monitor disabled");
        }
    }

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Separator();
    ImGui::TextDisabled("  TIP");
    ImGui::Spacing();
    ImGui::TextWrapped("Drag & drop a video/audio file anywhere onto this window to convert it to MP3.");

    ImGui::EndChild();
}

static void DrawToolbar() {
    ImGui::BeginChild("Toolbar", ImVec2(0, 52), true);
    if (ColoredButton("+ Add URL", COL_BLUE, ImVec2(110, 34))) { g_showAddUrlModal = true; }
    ImGui::SameLine();
    if (ImGui::Button("Resume Selected", ImVec2(130, 34))) { ShowToast("Select a row's Resume button"); }
    ImGui::SameLine();
    if (ColoredButton("Convert to MP3", COL_PURPLE, ImVec2(120, 34))) { g_showMp3Modal = true; g_mp3StatusMsg.clear(); }
    ImGui::SameLine();
    if (ImGui::Button("History", ImVec2(90, 34))) { g_showHistoryModal = true; }
    ImGui::SameLine();
    if (ImGui::Button("Scheduler", ImVec2(100, 34))) { g_showSchedulerModal = true; }
    ImGui::SameLine();
    if (ImGui::Button("Options", ImVec2(90, 34))) { g_showOptionsModal = true; strncpy_s(g_proxyInputBuf, g_proxy.c_str(), sizeof(g_proxyInputBuf)-1); }
    ImGui::SameLine();
    if (ImGui::Button("Legacy Protocol", ImVec2(130, 34))) {
        Engine_InstallBrowserIntegration();
        ShowToast("Browser protocol 'myidm://' registered");
    }
    ImGui::SameLine();
    if (ImGui::Button("Update Engine", ImVec2(120, 34))) {
        std::thread([](){ Engine_UpdateYtDlp(); }).detach();
        ShowToast("Updating yt-dlp in background...");
    }
    ImGui::SameLine();
    if (ImGui::Button("Speed Test", ImVec2(100, 34))) {
        std::thread([](){
            std::string r = Engine_RunSpeedTest();
            ShowToast(r);
        }).detach();
    }
    ImGui::EndChild();
}

static ImVec4 StatusColor(DownloadStatus s) {
    switch (s) {
        case DownloadStatus::Downloading: return COL_BLUE;
        case DownloadStatus::Completed:   return COL_GREEN;
        case DownloadStatus::Failed:      return COL_RED;
        case DownloadStatus::Cancelled:   return ImVec4(0.65f, 0.67f, 0.72f, 1.0f);
        case DownloadStatus::Paused:      return COL_ORANGE;
        default:                          return ImVec4(0.60f, 0.63f, 0.70f, 1.0f);
    }
}

static void DrawStatusPill(DownloadStatus s) {
    ImVec4 col = StatusColor(s);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float lineH = ImGui::GetTextLineHeight();
    float r = 4.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(pos.x + r, pos.y + lineH * 0.5f), r, ImGui::GetColorU32(col));
    ImGui::Dummy(ImVec2(r * 2 + 6, lineH));
    ImGui::SameLine(0, 0);
    ImGui::TextColored(col, "%s", Engine_StatusToString(s));
}

static void OpenContainingFolder(const std::shared_ptr<DownloadItem>& item) {
    std::string folder = !item->full_path.empty()
        ? item->full_path.substr(0, item->full_path.find_last_of("\\/"))
        : (g_download_root + "\\" + item->category);
    if (folder.empty()) folder = g_download_root;
    ShellExecuteW(NULL, L"open", Utf8ToWideLocal(folder).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static void DrawDownloadsTable() {
    ImGui::BeginChild("DownloadsArea", ImVec2(0, 0), true);

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                             ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("downloads_table", 7, flags)) {
        ImGui::TableSetupColumn("File Name", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("ETA", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 230);
        ImGui::TableHeadersRow();

        std::vector<std::shared_ptr<DownloadItem>> snapshot;
        {
            std::lock_guard<std::mutex> lock(g_downloads_mutex);
            snapshot = g_downloads;
        }

        int removeId = -1;
        std::shared_ptr<DownloadItem> retryItem, cancelItem, startItem, openFolderItem;

        for (auto& item : snapshot) {
            if (g_selectedCategory != "All Downloads" && item->category != g_selectedCategory) continue;

            ImGui::TableNextRow(ImGuiTableRowFlags_None, 34.0f);
            ImGui::PushID(item->id);

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(item->display_name.c_str());
            ImGui::TextDisabled("%s", item->url.substr(0, 60).c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(item->size_str.c_str());

            ImGui::TableSetColumnIndex(2);
            float progress = (float)(item->progress.load() / 100.0);
            char progBuf[32];
            snprintf(progBuf, sizeof(progBuf), "%.1f%%", item->progress.load());
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, StatusColor(item->status.load()));
            ImGui::ProgressBar(progress, ImVec2(-1, 20), progBuf);
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(item->speed_str.c_str());

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(item->eta_str.c_str());

            ImGui::TableSetColumnIndex(5);
            DrawStatusPill(item->status.load());

            ImGui::TableSetColumnIndex(6);
            DownloadStatus st = item->status.load();
            if (st == DownloadStatus::Queued) {
                if (ColoredSmallButton("Start", COL_GREEN)) startItem = item;
                ImGui::SameLine();
            }
            if (st == DownloadStatus::Downloading) {
                if (ColoredSmallButton("Stop", COL_RED)) cancelItem = item;
                ImGui::SameLine();
            }
            if (st == DownloadStatus::Failed || st == DownloadStatus::Cancelled) {
                if (ColoredSmallButton("Retry", COL_ORANGE)) retryItem = item;
                ImGui::SameLine();
            }
            if (st == DownloadStatus::Completed) {
                if (ColoredSmallButton("Open Folder", COL_GREEN)) openFolderItem = item;
                ImGui::SameLine();
            }
            if (ColoredSmallButton("Delete", COL_GRAY)) removeId = item->id;

            ImGui::PopID();
        }

        if (startItem) Engine_RetryDownload(startItem);
        if (cancelItem) Engine_CancelDownload(cancelItem);
        if (retryItem) Engine_RetryDownload(retryItem);
        if (openFolderItem) OpenContainingFolder(openFolderItem);
        if (removeId != -1) Engine_RemoveDownload(removeId);

        ImGui::EndTable();
    }
    ImGui::EndChild();
}

static void DrawAddUrlModal() {
    if (g_showAddUrlModal) ImGui::OpenPopup("Add URL");
    ImGui::SetNextWindowSize(ImVec2(520, 230), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Add URL", &g_showAddUrlModal)) {
        ImGui::TextWrapped("Paste a video/audio link (YouTube, TikTok, etc.) or a direct file link (.zip, .exe, .pdf...).");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##url", "https://...", g_urlInputBuf, sizeof(g_urlInputBuf));
        ImGui::Spacing();
        const char* qualities[] = { "Best (Highest Available)", "1080p", "720p", "480p", "Audio Only (MP3)" };
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##quality", &g_qualityChoiceIdx, qualities, IM_ARRAYSIZE(qualities));
        ImGui::Spacing();
        ImGui::Spacing();

        bool canSubmit = strlen(g_urlInputBuf) > 5;
        if (!canSubmit) ImGui::BeginDisabled();
        if (ColoredButton("Download Now", COL_BLUE, ImVec2(140, 32))) {
            Engine_AddDownload(g_urlInputBuf, QualityFromChoice(g_qualityChoiceIdx), true);
            ShowToast("Download started");
            g_urlInputBuf[0] = '\0';
            g_showAddUrlModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add to Queue", ImVec2(140, 32))) {
            Engine_AddDownload(g_urlInputBuf, QualityFromChoice(g_qualityChoiceIdx), false);
            ShowToast("Added to queue");
            g_urlInputBuf[0] = '\0';
            g_showAddUrlModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!canSubmit) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 32))) { g_showAddUrlModal = false; ImGui::CloseCurrentPopup(); }

        ImGui::EndPopup();
    }
}

static void DrawOptionsModal() {
    if (g_showOptionsModal) ImGui::OpenPopup("Options");
    ImGui::SetNextWindowSize(ImVec2(540, 600), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Options", &g_showOptionsModal)) {

        ImGui::BeginChild("OptionsScroll", ImVec2(0, -46), false);

        ImGui::Text("Proxy (leave blank for none):");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##proxy", "http://1.2.3.4:8080", g_proxyInputBuf, sizeof(g_proxyInputBuf));
        ImGui::Spacing();
        ImGui::TextDisabled("Downloads folder: %s", g_download_root.c_str());
        ImGui::Spacing();
        ImGui::Spacing();
        if (ColoredButton("Save", COL_BLUE, ImVec2(120, 32))) {
            g_proxy = g_proxyInputBuf;
            ShowToast(g_proxy.empty() ? "Proxy cleared" : "Proxy set");
        }

        ImGui::Dummy(ImVec2(0, 14));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        ImGui::TextColored(COL_BLUE, "BROWSER EXTENSION (Chrome / Edge)");
        ImGui::Spacing();
        ImGui::TextWrapped("1. Open chrome://extensions (or edge://extensions), enable Developer Mode, "
                            "click 'Load unpacked' and select the 'extension' folder.");
        ImGui::TextWrapped("2. Copy the Extension ID shown there and paste it below.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##extid", "e.g. abcdefghijklmnopabcdefghijklmnop", g_extensionIdBuf, sizeof(g_extensionIdBuf));
        ImGui::Spacing();
        if (ColoredButton("Register Native Messaging Host", COL_BLUE, ImVec2(-1, 34))) {
            std::string result;
            bool ok = Engine_InstallNativeMessagingHost(g_extensionIdBuf, result);
            g_nmhResultMsg = result;
            g_nmhResultOk = ok;
            ShowToast(result);
        }
        ImGui::Spacing();
        if (!g_nmhResultMsg.empty()) {
            ImVec4 col = g_nmhResultOk ? COL_GREEN : COL_RED;
            ImVec4 boxBg = ImVec4(col.x, col.y, col.z, 0.15f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, boxBg);
            ImGui::PushStyleColor(ImGuiCol_Border, col);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.5f);
            ImGui::BeginChild("nmh_result_box", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextWrapped("%s %s", g_nmhResultOk ? "[OK]" : "[FAILED]", g_nmhResultMsg.c_str());
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }
        ImGui::TextDisabled("(Requires UltimateDownloaderHost.exe built and sitting next to this app.)");

        ImGui::Dummy(ImVec2(0, 14));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextColored(COL_BLUE, "LEGACY LINK PROTOCOL (fallback)");
        ImGui::Spacing();
        if (ImGui::Button("Install 'myidm://' Protocol", ImVec2(-1, 32))) {
            Engine_InstallBrowserIntegration();
            ShowToast("Browser protocol 'myidm://' registered");
        }

        ImGui::EndChild(); // OptionsScroll

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));
        if (ImGui::Button("Close", ImVec2(120, 32))) { g_showOptionsModal = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

static void DrawHistoryModal() {
    if (g_showHistoryModal) ImGui::OpenPopup("Download History");
    ImGui::SetNextWindowSize(ImVec2(650, 420), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Download History", &g_showHistoryModal)) {
        if (ImGui::Button("Clear History")) Engine_ClearHistory();
        ImGui::SameLine();
        if (ImGui::Button("Close")) { g_showHistoryModal = false; ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        ImGui::BeginChild("history_scroll", ImVec2(0, 0), false);
        auto lines = Engine_ReadHistoryLines();
        for (auto& l : lines) ImGui::TextUnformatted(l.c_str());
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

static void DrawSchedulerModal() {
    if (g_showSchedulerModal) ImGui::OpenPopup("Scheduler");
    ImGui::SetNextWindowSize(ImVec2(420, 180), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Scheduler", &g_showSchedulerModal)) {
        ImGui::TextWrapped("Basic scheduler: start the queue automatically after N minutes.");
        static int minutes = 10;
        ImGui::SliderInt("Minutes", &minutes, 1, 180);
        if (ColoredButton("Arm Timer", COL_BLUE, ImVec2(120, 32))) {
            int mins = minutes;
            std::thread([mins](){
                Sleep(mins * 60 * 1000);
                Engine_StartAllQueued();
            }).detach();
            ShowToast("Queue will auto-start in " + std::to_string(minutes) + " min");
            g_showSchedulerModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 32))) { g_showSchedulerModal = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

static void DrawMp3Modal() {
    if (g_showMp3Modal) ImGui::OpenPopup("Convert Video to MP3");
    ImGui::SetNextWindowSize(ImVec2(600, 240), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Convert Video to MP3", &g_showMp3Modal)) {
        ImGui::TextWrapped("Choose a file, drag & drop one onto the app window, or paste a path directly (requires ffmpeg.exe next to the app).");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##mp3path", "C:\\path\\to\\video.mp4", g_mp3PathBuf, sizeof(g_mp3PathBuf));
        ImGui::Spacing();

        if (ImGui::Button("Browse...", ImVec2(120, 30))) {
            wchar_t fileBuf[MAX_PATH] = L"";
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g_hwnd;
            ofn.lpstrFilter = L"Media Files\0*.mp4;*.mkv;*.webm;*.avi;*.mov;*.flv;*.mp3;*.wav;*.flac;*.m4a;*.aac\0All Files\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle = L"Choose a video or audio file";
            if (GetOpenFileNameW(&ofn)) {
                std::string utf8 = WideToUtf8Local(fileBuf);
                strncpy_s(g_mp3PathBuf, utf8.c_str(), sizeof(g_mp3PathBuf) - 1);
                g_mp3StatusMsg.clear();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("or drop a file anywhere on the window");

        ImGui::Spacing();
        if (!g_mp3StatusMsg.empty()) ImGui::TextWrapped("%s", g_mp3StatusMsg.c_str());
        ImGui::Spacing();
        bool canConvert = strlen(g_mp3PathBuf) > 3;
        if (!canConvert) ImGui::BeginDisabled();
        if (ColoredButton("Convert", COL_PURPLE, ImVec2(120, 32))) {
            std::string pathUtf8(g_mp3PathBuf);
            std::thread([pathUtf8]() {
                std::wstring wpath = Utf8ToWideLocal(pathUtf8);
                std::wstring outPath; std::string err;
                bool ok = Engine_ConvertToMp3(wpath, outPath, err);
                ShowToast(ok ? "MP3 conversion complete" : err);
            }).detach();
            g_mp3StatusMsg = "Converting in background... check the toast notification.";
        }
        if (!canConvert) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 32))) { g_showMp3Modal = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

static void DrawToast() {
    if (g_toastTimer <= 0.0) return;
    ImGuiIO& io = ImGui::GetIO();
    g_toastTimer -= io.DeltaTime;

    float alpha = (float)std::min(g_toastTimer, 1.0) / 1.0f;
    alpha = std::max(0.0f, std::min(1.0f, alpha));

    ImVec2 textSize = ImGui::CalcTextSize(g_toastMessage.c_str());
    ImVec2 padding = ImVec2(16, 10);
    ImVec2 boxSize = ImVec2(textSize.x + padding.x * 2, textSize.y + padding.y * 2);
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 60);
    ImVec2 boxMin = ImVec2(center.x - boxSize.x * 0.5f, center.y - boxSize.y * 0.5f);
    ImVec2 boxMax = ImVec2(boxMin.x + boxSize.x, boxMin.y + boxSize.y);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 bgCol = ImGui::GetColorU32(ImVec4(0.10f, 0.11f, 0.15f, 0.92f * alpha));
    ImU32 borderCol = ImGui::GetColorU32(ImVec4(COL_BLUE.x, COL_BLUE.y, COL_BLUE.z, alpha));
    ImU32 textCol = ImGui::GetColorU32(ImVec4(0.92f, 0.95f, 1.0f, alpha));

    dl->AddRectFilled(boxMin, boxMax, bgCol, 8.0f);
    dl->AddRect(boxMin, boxMax, borderCol, 8.0f, 0, 1.5f);
    dl->AddText(ImVec2(boxMin.x + padding.x, boxMin.y + padding.y), textCol, g_toastMessage.c_str());
}

static void RenderUI() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("MainWindow", nullptr, flags);

    ImGui::Columns(2, "layout_cols", false);
    ImGui::SetColumnWidth(0, 210);

    DrawSidebar();
    ImGui::NextColumn();

    DrawToolbar();
    DrawDownloadsTable();

    ImGui::Columns(1);
    ImGui::End();

    DrawAddUrlModal();
    DrawOptionsModal();
    DrawHistoryModal();
    DrawSchedulerModal();
    DrawMp3Modal();
    DrawToast();
}

// ============================================================
//  Single-instance / myidm:// protocol forwarding
// ============================================================
static void ForwardUrlToRunningInstance(const std::string& url) {
    HWND existing = FindWindowW(kWindowClassName, nullptr);
    if (!existing) return;
    COPYDATASTRUCT cds;
    cds.dwData = 1;
    cds.cbData = (DWORD)url.size() + 1;
    cds.lpData = (void*)url.c_str();
    SendMessageW(existing, WM_COPYDATA, 0, (LPARAM)&cds);
    ShowWindow(existing, SW_RESTORE);
    SetForegroundWindow(existing);
}

static std::string ParseMyIdmUrl(const std::wstring& arg) {
    std::string url = WideToUtf8Local(arg);
    if (url.rfind("myidm://", 0) == 0) {
        url = url.substr(8);
        if (!url.empty() && url.back() == '/') url.pop_back();
        if (url.rfind("https//", 0) == 0) url.replace(0, 7, "https://");
        if (url.rfind("http//", 0) == 0) url.replace(0, 6, "http://");
        return url;
    }
    return "";
}

// ============================================================
//  main()
// ============================================================
int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    std::string startupUrl;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            std::string u = ParseMyIdmUrl(argv[i]);
            if (!u.empty()) { startupUrl = u; break; }
        }
        LocalFree(argv);
    }

    // ---- Enforce a single running instance; forward the URL to it if one exists ----
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    bool alreadyRunning = (GetLastError() == ERROR_ALREADY_EXISTS);
    if (alreadyRunning) {
        if (!startupUrl.empty()) {
            ForwardUrlToRunningInstance(startupUrl);
        } else {
            HWND existing = FindWindowW(kWindowClassName, nullptr);
            if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    Engine_Init();
    Engine_StartInboxWatcher();

    HICON hAppIcon = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr),
                        hAppIcon, nullptr, nullptr, nullptr, kWindowClassName, hAppIcon };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Ultimate Downloader",
                               WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800,
                               nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = hwnd;
    DragAcceptFiles(hwnd, TRUE);

    if (!startupUrl.empty()) {
        Engine_AddDownload(startupUrl, QualityFromChoice(0), true);
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyTheme();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        const float clear_color[4] = { 0.09f, 0.10f, 0.13f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    Engine_StopClipboardMonitor();
    Engine_StopInboxWatcher();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (hMutex) CloseHandle(hMutex);
    return 0;
}

// ============================================================
//  D3D11 boilerplate
// ============================================================
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_COPYDATA: {
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
        if (cds && cds->lpData) {
            std::string url((const char*)cds->lpData);
            if (!url.empty()) Engine_AddDownload(url, QualityFromChoice(0), true);
        }
        return 0;
    }
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        wchar_t path[MAX_PATH] = L"";
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH)) {
            std::string utf8 = WideToUtf8Local(path);
            strncpy_s(g_mp3PathBuf, utf8.c_str(), sizeof(g_mp3PathBuf) - 1);
            g_mp3StatusMsg.clear();
            g_showMp3Modal = true;
        }
        DragFinish(hDrop);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
