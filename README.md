<div align="center">

# Ultimate Downloader

A fast download manager for Windows — video, audio, and direct file downloads, with a
browser extension for one-click grabs. No installer, no bloat: unzip and run.

[![Release](https://img.shields.io/github/v/release/YOUR_USERNAME/UltimateDownloader?label=latest%20release)](../../releases/latest)
[![Downloads](https://img.shields.io/github/downloads/YOUR_USERNAME/UltimateDownloader/total)](../../releases)
[![License](https://img.shields.io/github/license/YOUR_USERNAME/UltimateDownloader)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-0078D6)](#installation)

</div>

---

<p align="center">
  <img src="docs/screenshots/main-window.png" width="850" alt="Ultimate Downloader main window">
</p>

## Features

- **Universal downloads** — paste a YouTube/TikTok/etc. link and pick a quality (Best,
  1080p, 720p, 480p, or Audio-only MP3), or drop in a direct file link (`.zip`, `.exe`,
  `.pdf`, ...) and it's routed automatically
- **Live progress** for every download — real percentage, speed, and ETA, not a spinner
- **Browser extension** (Chrome / Edge / Brave / any Chromium browser) — right-click any
  video or link on a page and send it straight to the app, or use the popup to see every
  media link the page detected
- **Works with any browser via a bookmarklet + legacy protocol** (see below) — including
  Firefox, which doesn't support the extension's native messaging setup the same way
- **MP3 converter** — pick a file with the native file dialog, or just drag & drop a
  video/audio file onto the app window; real ffmpeg errors on failure, not a guess
- **Clipboard auto-monitor** — copy a link, it starts downloading on its own (optional)
- **Queue + scheduler** — batch a list of links and start them together, or on a timer
- **Download history**, category sidebar (Video / Music / Compressed / Documents),
  one-click **Open Folder** on anything finished
- Single-instance aware — clicking a link twice focuses the existing window instead of
  opening a duplicate

## Installation

1. Download the latest ZIP from **[Releases](../../releases/latest)**
2. Extract it anywhere (your Desktop, `C:\Apps`, wherever)
3. Run **`UltimateDownloaderGUI.exe`** — no installer, no admin rights needed
4. On first launch it creates a `Downloads` folder next to itself, with subfolders for
   Video / Music / Compressed / Documents / Other

That's it for basic use. For one-click downloading from your browser, set up **one** of
the two methods below.

### Option A — Browser extension (Chrome, Edge, Brave, any Chromium browser)

1. Go to `chrome://extensions` (or `edge://extensions`, `brave://extensions`)
2. Turn on **Developer mode** (top-right toggle)
3. Click **Load unpacked** and select the `extension` folder from the ZIP you extracted
4. Copy the **Extension ID** shown on the card that appears
5. In the app: **Options** → paste the Extension ID → **Register Native Messaging Host**
6. Reload the extension — right-click any video or link and choose **Download with
   Ultimate Downloader**, or click the extension's toolbar icon to see everything
   detected on the current page

### Option B — Legacy protocol (works in *any* browser, including Firefox)

If you don't want to install the extension, or you're on a browser it doesn't support:

1. Open your browser's **Bookmark Manager** and add a new bookmark (name it anything,
   e.g. "Send to UDM")
2. For its URL/location, paste this exactly:
   ```
   javascript:(function(){window.location.href='myidm://'+window.location.href;})();
   ```
3. Save the bookmark
4. Open `UltimateDownloaderGUI.exe`, go to **Options**, and click **Install 'myidm://'
   Protocol** — you'll see a confirmation that registration completed
5. Now, on any page you want to download, click that bookmark — it hands the current
   page's URL to Ultimate Downloader directly. If the app is already open, the link is
   sent straight to it instead of opening a second window.

## Building from source

Want to build it yourself instead of using the release ZIP? See
[`docs/BUILDING.md`](docs/BUILDING.md) — covers getting Dear ImGui, libcurl via vcpkg,
the CMake build, and (optionally) packaging your own installer.

## How it works

- Direct file downloads use libcurl directly for real byte-level progress
- Video/audio downloads wrap [yt-dlp](https://github.com/yt-dlp/yt-dlp), with its
  progress output parsed live to drive the UI
- The browser extension talks to the app over Chrome's
  [Native Messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging)
  API — no scary "open this app?" prompts, no registry hacks needing admin rights

## Troubleshooting

- **App won't start / missing DLL error** — make sure `libcurl.dll` and `z.dll` are in
  the same folder as `UltimateDownloaderGUI.exe`. They ship with the release ZIP; if
  you moved the exe on its own, grab them back from the ZIP.
- **Downloads don't start** — check that `yt-dlp.exe` is in the same folder. It's
  included in the release ZIP, but some antivirus tools quarantine it on extraction
  (false positive, common for yt-dlp) — check your AV's quarantine list.
- **MP3 converter fails** — needs `ffmpeg.exe` in the same folder. Also included in
  the ZIP.
- **Extension "Register Native Messaging Host" fails** — check `chrome://extensions` →
  **Errors** on the extension's card. Usually the Extension ID you pasted doesn't match
  the one currently loaded (happens if you reload the extension and Chrome assigns a
  new ID) — just re-copy the current ID and register again.

## Contributing

Issues and PRs welcome. If you're changing the UI, keep it consistent with the existing
dark theme (`ApplyTheme()` in `src/main.cpp`) and the color palette (`COL_BLUE`,
`COL_GREEN`, `COL_RED`, etc.) rather than introducing new ad-hoc colors.

## License

[MIT](LICENSE) — do what you want with it, just don't hold me liable.

## Acknowledgments

- [Dear ImGui](https://github.com/ocornut/imgui) for the UI toolkit
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) for the actual video/audio extraction
- [FFmpeg](https://ffmpeg.org/) for media conversion
- [libcurl](https://curl.se/libcurl/) for direct downloads
