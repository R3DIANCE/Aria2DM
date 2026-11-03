Aria2DM browser integration
===========================

The desktop side is already registered. Load the extension once in each
browser you want to use:

Chromium browsers (Chrome, Edge, Brave, Vivaldi, Opera, Chromium)
------------------------------------------------------------------
1. Open the browser's Extensions page.
   Chrome/Edge/Brave:  chrome://extensions  or  edge://extensions
2. Turn on "Developer mode".
3. Choose "Load unpacked" and select the "chromium" folder next to this file.
4. Keep Aria2DM running in the tray.

Firefox
-------
1. Open about:debugging#/runtime/this-firefox
2. Choose "Load Temporary Add-on..." and select "firefox/manifest.json".
   For a permanent install, zip the "firefox" folder and install it as a
   signed add-on, or use about:config:
   xpinstall.signatures.required = false for a local developer build.

What it does
------------
- Cancels downloads the browser starts and sends the URL to Aria2DM.
- Captures clicks on Google Drive file links, MEGA file links, MediaFire
  links, and direct archive/media files.
- Adds a "Download with Aria2DM" right-click menu item for links and media.

Notes
-----
- Google Drive links are converted to the direct usercontent download URL.
- MediaFire pages are scraped for the real download URL.
- MEGA files are decrypted locally by Aria2DM (AES-CTR). If a link does not
  contain its key, Aria2DM asks for the decryption key.
