// Aria2DM browser bridge (Firefox).

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
  browser.runtime
    .sendNativeMessage(HOST, {
      type: "download",
      url,
      referrer: (meta && meta.referrer) || "",
      filename: (meta && meta.filename) || ""
    })
    .then((response) => {
      if (response && response.error) console.warn("Aria2DM:", response.error);
    })
    .catch((error) => console.warn("Aria2DM host unavailable:", error));
}

browser.downloads.onCreated.addListener(async (item) => {
  const url = item.finalUrl || item.url;
  if (!url || !/^https?:\/\//i.test(url)) return;
  try {
    await browser.downloads.cancel(item.id);
    await browser.downloads.erase({ id: item.id });
  } catch (error) {
    // The download may already have finished or been removed.
  }
  sendToApp(url, { referrer: item.referrer, filename: item.filename });
});

browser.runtime.onMessage.addListener((message, sender) => {
  if (message && message.type === "aria2dm-download" && message.url) {
    sendToApp(message.url, {
      referrer: sender.tab ? sender.tab.url : "",
      filename: message.filename || ""
    });
  }
});

browser.runtime.onInstalled.addListener(() => {
  browser.contextMenus.create({
    id: "aria2dm-link",
    title: "Download with Aria2DM",
    contexts: ["link", "video", "audio", "image"]
  });
});

browser.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId !== "aria2dm-link") return;
  const url = info.linkUrl || info.srcUrl;
  if (url) sendToApp(url, { referrer: info.pageUrl });
});
