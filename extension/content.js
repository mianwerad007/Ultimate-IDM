function isUsableMediaUrl(url) {
  // blob: and data: URLs point at in-page memory, not a real network
  // location — yt-dlp/curl on the app side can't fetch them. Only http(s)
  // links are actually downloadable outside the page.
  return /^https?:\/\//i.test(url);
}

function collectMediaLinks() {
  const links = new Set();
  let hasStreamingPlayer = false;

  document.querySelectorAll("video, audio").forEach((el) => {
    const candidate = el.currentSrc || el.src;
    if (candidate && isUsableMediaUrl(candidate)) {
      links.add(candidate);
    } else if (candidate) {
      // Has a video/audio element, but its src is blob:/data: (common on
      // YouTube, Netflix-style MSE players). Not directly downloadable.
      hasStreamingPlayer = true;
    }
    el.querySelectorAll("source").forEach((s) => {
      if (s.src && isUsableMediaUrl(s.src)) links.add(s.src);
    });
  });

  document.querySelectorAll("a[href]").forEach((a) => {
    const href = a.href;
    if (
      isUsableMediaUrl(href) &&
      /\.(mp4|mkv|webm|m4v|mov|flv|ts|zip|exe|msi|rar|7z|pdf|mp3|m4a|wav|m3u8)(\?|$)/i.test(href)
    ) {
      links.add(href);
    }
  });

  return { links: Array.from(links), hasStreamingPlayer };
}

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (msg.type === "GET_MEDIA_LINKS") {
    const { links, hasStreamingPlayer } = collectMediaLinks();
    sendResponse({ links, pageUrl: location.href, hasStreamingPlayer });
  }
  return true;
});
