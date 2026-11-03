// Capture clicks on sharing-page links and direct download files before the
// browser navigates to them. The background worker forwards the URL to the
// native Aria2DM host.
(() => {
  const providerPatterns = [
    /^https?:\/\/drive\.google\.com\/file\//i,
    /^https?:\/\/drive\.google\.com\/open\?/i,
    /^https?:\/\/drive\.google\.com\/uc\?/i,
    /^https?:\/\/mega\.nz\/(file|folder)\//i,
    /^https?:\/\/mega\.nz\/#(!|F!)/i,
    /^https?:\/\/[^/]*mediafire\.com\/(file|download)\//i
  ];
  const filePattern = /\.(zip|rar|7z|tar|gz|bz2|xz|iso|img|exe|msi|apk|dmg|pkg|torrent|mp4|mkv|avi|mov|webm|flac|mp3|wav|opus|pdf)$/i;

  document.addEventListener(
    "click",
    (event) => {
      const anchor = event.target && event.target.closest ? event.target.closest("a[href]") : null;
      if (!anchor) return;
      const url = anchor.href;
      if (!url || !/^https?:\/\//i.test(url)) return;

      const provider = providerPatterns.some((pattern) => pattern.test(url));
      const directFile = !provider && filePattern.test(url.split("?")[0].split("#")[0]);
      if (!provider && !directFile) return;

      event.preventDefault();
      event.stopPropagation();
      chrome.runtime.sendMessage({
        type: "aria2dm-download",
        url,
        filename: anchor.getAttribute("download") || ""
      });
    },
    true
  );
})();
