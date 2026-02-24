# Ultimate-IDM
Ultimate IDM: Advanced C++ Download Manager with multi-thread support, 4K video downloading via yt-dlp, and automated parallel queue processing
## 🚀 About Ultimate IDM (UDM)
Ultimate IDM is a professional-grade download manager developed in C++ to provide maximum speed and versatility. It combines the raw power of `aria2c` with the flexibility of `yt-dlp`, all managed through a custom-built C++ core.

### 🛠 Why Choose UDM?
* **High-Speed Transfers:** Utilizes multi-connection downloading to saturate your bandwidth.
* **Universal Media Grabber:** Download 4K videos or extract MP3s from almost any platform.
* **Smart Monitoring:** No need to manually paste links; the app watches your clipboard for you.
* **Advanced Networking:** Full support for custom proxies and the `myidm://` protocol.
  
📥 How to Install & Run
Follow these steps to get UDM running on your system:

Download the Package:

Navigate to the Releases section on the right side of this GitHub repository.

Download the latest UDM_Portable.zip file.

Extract the Files:

Right-click the downloaded .zip file and select Extract All.

Important: Keep all files together in the same folder. Do not move downloader.exe away from its support files.

Verify Dependencies:

Ensure the following files are present in the extracted folder to avoid "Entry Point Not Found" errors:

downloader.exe (The main application)

aria2c.exe & yt-dlp.exe (The core engines)

Required DLLs: libwinpthread-1.dll, libgcc_s_seh-1.dll, and libstdc++-6.dll

Run the Application:

Double-click downloader.exe to launch the V13 Gold interface
🛠 Troubleshooting
If you receive a "Procedure entry point nanosleep64 could not be located" error:

Ensure your Windows OS is fully updated.

Install the Microsoft Visual C++ Redistributable.

Confirm that libwinpthread-1.dll is in the same directory as your .exe.

