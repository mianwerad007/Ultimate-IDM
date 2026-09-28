function send(url, quality, onDone) {
  chrome.runtime.sendMessage({ type: "SEND_TO_APP", url, quality }, (result) => {
    if (chrome.runtime.lastError) {
      onDone({ ok: false, error: chrome.runtime.lastError.message });
      return;
    }
    onDone(result || { ok: false, error: "No response from extension" });
  });
}

function describeResult(result) {
  if (!result) return { text: "No response.", ok: false };
  if (result.ok && result.method === "native") return { text: "Sent to the app.", ok: true };
  if (result.ok && result.method === "fallback") return { text: "App unreachable — downloaded via browser instead.", ok: true };
  if (result.method === "fallback") return { text: "App unreachable, and browser download failed too.", ok: false };
  return { text: "Failed: " + (result.error || "unknown error"), ok: false };
}

const manualStatus = document.getElementById("manualStatus");
const sendManualBtn = document.getElementById("sendManual");

sendManualBtn.addEventListener("click", () => {
  const urlInput = document.getElementById("manualUrl");
  const url = urlInput.value.trim();
  const quality = document.getElementById("quality").value;
  if (!url) {
    manualStatus.textContent = "Paste a link first.";
    manualStatus.className = "status err";
    return;
  }

  sendManualBtn.disabled = true;
  sendManualBtn.textContent = "Sending...";
  manualStatus.textContent = "";
  manualStatus.className = "status";

  send(url, quality, (result) => {
    const { text, ok } = describeResult(result);
    manualStatus.textContent = text;
    manualStatus.className = "status " + (ok ? "ok" : "err");
    sendManualBtn.disabled = false;
    sendManualBtn.textContent = "Send to Ultimate Downloader";
    if (ok) setTimeout(() => window.close(), 900);
  });
});

function makeRow(label, url, quality, isPageRow) {
  const row = document.createElement("div");
  row.className = "link-row" + (isPageRow ? " page-row" : "");

  const span = document.createElement("span");
  span.className = "link-text";
  span.textContent = label;
  span.title = url;

  const btn = document.createElement("button");
  btn.textContent = "Send";
  btn.addEventListener("click", () => {
    btn.disabled = true;
    btn.textContent = "...";
    send(url, quality, (result) => {
      const { text, ok } = describeResult(result);
      btn.textContent = ok ? "Sent" : "Failed";
      btn.title = text;
      if (!ok) btn.disabled = false;
    });
  });

  row.appendChild(span);
  row.appendChild(btn);
  return row;
}

chrome.tabs.query({ active: true, currentWindow: true }, (tabs) => {
  const container = document.getElementById("links");
  if (!tabs[0]) {
    container.innerHTML = "<div class='empty'>No active tab.</div>";
    return;
  }

  chrome.tabs.sendMessage(tabs[0].id, { type: "GET_MEDIA_LINKS" }, (response) => {
    container.innerHTML = "";

    if (chrome.runtime.lastError || !response) {
      container.innerHTML =
        "<div class='empty'>Can't scan this page (browser pages, the Web Store, and PDF " +
        "viewers can't be scanned). Paste a link below instead.</div>";
      return;
    }

    // If the page has a streaming player (blob: src, e.g. YouTube-style sites),
    // the raw media src isn't downloadable — send the page URL itself so
    // yt-dlp can extract it properly.
    if (response.hasStreamingPlayer) {
      container.appendChild(
        makeRow("This page (streaming player detected)", response.pageUrl, "best", true)
      );
    }

    if (!response.links.length) {
      if (!response.hasStreamingPlayer) {
        container.innerHTML += "<div class='empty'>No media links detected on this page.</div>";
      }
      return;
    }

    response.links.forEach((link) => {
      container.appendChild(makeRow(link, link, "best", false));
    });
  });
});
