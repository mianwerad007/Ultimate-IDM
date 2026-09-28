// Must match the "name" field in native_host_manifest.json and the
// registry key name used when installing the host from the app's Options panel.
const NATIVE_HOST = "com.ultimatedownloader.native";

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: "ud-download-link",
    title: "Download with Ultimate Downloader",
    contexts: ["link", "video", "audio", "page"]
  });
});

chrome.contextMenus.onClicked.addListener((info) => {
  const url = info.linkUrl || info.srcUrl || info.pageUrl;
  if (url) sendToApp(url, "best");
});

function notify(message) {
  chrome.notifications.create({
    type: "basic",
    iconUrl: "icons/icon48.png",
    title: "Ultimate Downloader",
    message
  });
}

function shortenReason(reason) {
  if (!reason) return "unknown reason";
  if (/not found|Specified native messaging host not found/i.test(reason)) return "app not registered";
  if (/forbidden|not allowed to open this native/i.test(reason)) return "Extension ID mismatch — re-register in the app's Options";
  return reason;
}

// If the desktop app / native host isn't reachable, fall back to a plain
// browser download instead of silently doing nothing.
function fallbackDownload(url, reason) {
  console.warn("Ultimate Downloader: falling back to browser download —", reason);
  return new Promise((resolve) => {
    try {
      chrome.downloads.download({ url }, (downloadId) => {
        if (chrome.runtime.lastError || !downloadId) {
          notify("Couldn't reach the app (" + shortenReason(reason) + "), and the browser download also failed.");
          resolve({ ok: false, method: "fallback", error: reason });
        } else {
          notify("App not found (" + shortenReason(reason) + ") — downloaded via the browser instead.");
          resolve({ ok: true, method: "fallback", error: reason });
        }
      });
    } catch (err) {
      notify("Couldn't reach the app, and the fallback download failed.");
      resolve({ ok: false, method: "fallback", error: reason });
    }
  });
}

// Sends a URL to the desktop app via Native Messaging.
// Always resolves (never rejects) with { ok, method, error? } so callers
// don't need try/catch and always get a definite answer to show the user.
function sendToApp(url, quality) {
  return new Promise((resolve) => {
    let settled = false;
    const finish = (result) => {
      if (settled) return;
      settled = true;
      resolve(result);
    };

    // Native messaging can hang if the host launches but never replies
    // (e.g. the GUI app is stuck). Don't leave the caller waiting forever.
    const timeoutId = setTimeout(async () => {
      finish(await fallbackDownload(url, "App didn't respond in time"));
    }, 4000);

    try {
      chrome.runtime.sendNativeMessage(NATIVE_HOST, { url, quality }, async (response) => {
        clearTimeout(timeoutId);
        if (settled) return;

        if (chrome.runtime.lastError) {
          const reason = chrome.runtime.lastError.message || "Unknown native messaging error";
          console.error("Ultimate Downloader native host error:", reason);
          finish(await fallbackDownload(url, reason));
          return;
        }
        if (response && response.status === "error") {
          notify("The app rejected the link: " + (response.message || "unknown error"));
          finish({ ok: false, method: "native", error: response.message || "App reported an error" });
          return;
        }
        notify("Sent to the app.");
        finish({ ok: true, method: "native" });
      });
    } catch (err) {
      clearTimeout(timeoutId);
      fallbackDownload(url, err.message).then(finish);
    }
  });
}

// Popup and content script both talk to us via chrome.runtime.sendMessage
chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (msg.type === "SEND_TO_APP") {
    sendToApp(msg.url, msg.quality || "best").then(sendResponse);
    return true; // keep the message channel open for the async response
  }
  return false;
});
