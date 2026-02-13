# Ultimate-IDM
Ultimate IDM: Advanced C++ Download Manager with multi-thread support, 4K video downloading via yt-dlp, and automated parallel queue processing
## 🚀 About Ultimate IDM (UDM)
Ultimate IDM is a professional-grade download manager developed in C++ to provide maximum speed and versatility. It combines the raw power of `aria2c` with the flexibility of `yt-dlp`, all managed through a custom-built C++ core.

### 🛠 Why Choose UDM?
* **High-Speed Transfers:** Utilizes multi-connection downloading to saturate your bandwidth.
* **Universal Media Grabber:** Download 4K videos or extract MP3s from almost any platform.
* **Smart Monitoring:** No need to manually paste links; the app watches your clipboard for you.
* **Advanced Networking:** Full support for custom proxies and the `myidm://` protocol.
* 
📥 How to Install & Run
Download the Release: Go to the Releases section on the right side of this GitHub page and download the UDM_Portable.zip file.

Extract the Files: Right-click the downloaded .zip and select Extract All.

Check Dependencies: Ensure that aria2c.exe, yt-dlp.exe, and the included .dll files are in the same folder as downloader.exe.

Run the App: Double-click downloader.exe to start.

🛠 Troubleshooting: "Entry Point Not Found"
If you see an error mentioning nanosleep64:

Update Windows: Ensure your Windows 10 or 11 is up to date.

Install Runtimes: Download and install the Microsoft Visual C++ Redistributable.

Check DLLs: Make sure libwinpthread-1.dll is present in the application folder.
