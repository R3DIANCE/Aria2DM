#pragma once

#include <string>

// Runs the Chrome/Firefox native messaging protocol on stdin/stdout. The
// browser launches this same executable with the extension origin as its first
// argument, so main() routes here before any window is created.
bool RunNativeMessagingHost();

// Writes the bundled extension files and registers the native host manifest
// for every supported Chromium browser and Firefox. Per-user only, no admin.
bool InstallBrowserIntegration(const std::wstring& dataDir, std::wstring& message);
