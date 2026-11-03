// Aria2DM browser bridge (Chromium MV3).
//
// Two things are handed to the desktop app:
//   1. Downloads the browser is about to start (chrome.downloads.onCreated).
//   2. Links clicked on supported sharing sites (content.js).
// The native host forwards them to the running Aria2DM window.

const HOST = "com.aria2dm.host";
const recent = new Map();

function alreadySent(url) {
  const now = Date.now();
  for (const [key, time] of recent) {
    if (now - time > 30000) recent.delete(key);
  }
  if (recent.has(url)) return true;
  recent.set(url, now);
  return false;
}

function sendToApp(url, meta) {
  if (!url || !/^https?:\/\//i.test(url) || alreadySent(url)) return;
  try {
    chrome.runtime.sendNativeMessage(
      HOST,
      {
        type: "download",
        url,
        referrer: (meta && meta.referrer) || "",
        filename: (meta && meta.filename) || ""
      },
      (response) => {
        if (chrome.runtime.lastError) {
          console.warn("Aria2DM host unavailable:", chrome.runtime.lastError.message);
        } else if (response && response.error) {
          console.warn("Aria2DM:", response.error);
        }
      }
    );
  } catch (error) {
    console.warn("Aria2DM:", error);
  }
}

// Any download Chrome is about to save is cancelled and sent to Aria2DM.
chrome.downloads.onCreated.addListener((item) => {
  if (item.byExtensionId) return;
  const url = item.finalUrl || item.url;
  if (!url || !/^https?:\/\//i.test(url)) return;
  chrome.downloads.cancel(item.id, () => {
    chrome.downloads.erase({ id: item.id }, () => {});
  });
  sendToApp(url, { referrer: item.referrer, filename: item.filename });
});

chrome.runtime.onMessage.addListener((message, sender) => {
  if (message && message.type === "aria2dm-download" && message.url) {
    sendToApp(message.url, {
      referrer: sender.tab ? sender.tab.url : "",
      filename: message.filename || ""
    });
  }
});

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: "aria2dm-link",
    title: "Download with Aria2DM",
    contexts: ["link", "video", "audio", "image"]
  });
});

chrome.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId !== "aria2dm-link") return;
  const url = info.linkUrl || info.srcUrl;
  if (url) sendToApp(url, { referrer: info.pageUrl });
});
