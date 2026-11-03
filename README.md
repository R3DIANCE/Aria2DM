# Aria2DM

<img width="1058" height="632" alt="explorer_0suE5g07pU" src="https://github.com/user-attachments/assets/9b79e8a0-6f4e-45d1-96dc-016051ee029d" />

### A lightweight, native Windows download manager powered by aria2.

**Aria2DM** is a native Windows download manager built around the powerful [aria2](https://github.com/aria2/aria2) download engine. It combines aria2's multi-protocol downloading capabilities with a desktop application, browser integration, and specialized download-provider resolvers.

Designed to provide a convenient graphical downloading experience without sacrificing the flexibility and performance of aria2.

---

## Overview

Aria2DM bridges the gap between traditional download managers and the flexibility of command-line download engines.

Instead of manually copying URLs into a terminal or configuring an external aria2 process, Aria2DM provides a native Windows application that handles download management, link resolution, and browser communication.

Its architecture combines:

- **Native Windows application** – A standalone desktop executable.
- **aria2 integration** – Download management through aria2's JSON-RPC interface.
- **Browser integration** – Native Messaging support for Chromium-based browsers and Firefox.
- **Download provider resolution** – Specialized handling for supported file-hosting services.
- **MEGA decryption and streaming** – Dedicated handling for encrypted MEGA file downloads.
- **Lightweight networking** – Windows-native networking APIs.

## Features

### Download Management

Aria2DM uses aria2 as its underlying download engine, providing access to its established download capabilities.

- Add downloads using direct URLs.
- Download HTTP and HTTPS resources.
- Support magnet links and BitTorrent downloads through aria2.
- Pause and resume active downloads.
- Remove active downloads and completed download records.
- Retry failed downloads.
- Monitor download progress.
- Track download and upload speeds.
- Display connection information.
- Monitor download status and errors.
- Clear completed download history.

### Download Provider Integration

Aria2DM includes dedicated URL resolvers for supported file-hosting platforms.

| Provider | Support | Description |
|---|---|---|
| Direct URLs | Supported | Standard HTTP/HTTPS downloads |
| MEGA | Supported | Individual encrypted file links |
| Google Drive | Supported | Shared file links |
| MediaFire | Supported | Download page resolution |
| BitTorrent | Supported through aria2 | Torrent downloads |
| Magnet | Supported through aria2 | Magnet URI downloads |

#### MEGA

Aria2DM implements dedicated MEGA link handling instead of treating MEGA URLs as ordinary HTTP downloads.

Capabilities include:

- MEGA file URL parsing.
- File handle extraction.
- Decryption key extraction.
- AES-CTR decryption.
- Random-access file streaming.
- Local HTTP proxy integration.
- Download filename handling.

**Note:** MEGA folder links are currently not supported. Individual files must be added separately.

#### Google Drive

Google Drive links are normalized into direct download URLs.

Supported functionality includes:

- Extracting Google Drive file IDs.
- Handling shared file URLs.
- Preserving resource keys.
- Resolving direct download endpoints.
- Retrieving filename and content-length metadata when available.

#### MediaFire

MediaFire URLs are resolved by inspecting the download page and identifying the actual file download endpoint.

The resolver includes:

- Download page retrieval.
- HTML entity decoding.
- Direct download URL extraction.
- Download host validation.
- Filename extraction.
- HTTP metadata retrieval when available.

---

## Browser Integration

Aria2DM includes a browser integration system based on the Native Messaging API.

This allows compatible browser extensions to communicate directly with the desktop application.

Instead of requiring users to manually copy download links, the browser integration forwards supported HTTP and HTTPS URLs to Aria2DM.

### Supported Browsers

| Browser | Integration |
|---|---|
| Google Chrome | Supported |
| Microsoft Edge | Supported |
| Brave | Supported |
| Vivaldi | Supported |
| Chromium | Supported |
| Opera | Supported |
| Mozilla Firefox | Supported |

### How It Works

The integration uses three components:

1. **Browser Extension** – Captures and forwards download URLs.
2. **Native Messaging Host** – Communicates between the browser and desktop application.
3. **Aria2DM Application** – Receives URLs and manages the download.

The native host uses standard input/output communication with the browser and Windows messaging to forward requests to the running application.

### Browser Installation

Launch Aria2DM and execute the browser integration installation option.

Alternatively, run:

```powershell
.\aria2dm.exe --install-browser
```

The application generates its browser integration files inside its application data directory:

```text
browser/
├── chromium/
│   ├── manifest.json
│   ├── background.js
│   └── content.js
│
├── firefox/
│   ├── manifest.json
│   ├── background.js
│   └── content.js
│
├── chromium-host.json
├── firefox-host.json
└── README.txt
```

For Chromium-based browsers, open the extensions page:

```text
chrome://extensions
```

Enable **Developer mode**, select **Load unpacked**, and select the generated `browser/chromium` directory.

For Firefox, use:

```text
about:debugging#/runtime/this-firefox
```

Load the generated Firefox extension manifest.

Restart or refresh the browser extension if necessary.

---

## Architecture

Aria2DM is implemented in native C++ and uses Windows APIs for application management, networking, browser communication, and rendering.

### High-Level Architecture

```text
                  ┌──────────────────────┐
                  │    Web Browser       │
                  │                      │
                  │ Chrome / Edge        │
                  │ Brave / Firefox      │
                  └──────────┬───────────┘
                             │
                       Native Messaging
                             │
                  ┌──────────▼───────────┐
                  │   BrowserHost        │
                  │                      │
                  │ Native Host Bridge   │
                  │ Extension Installer  │
                  └──────────┬───────────┘
                             │
                       Windows IPC
                             │
                  ┌──────────▼───────────┐
                  │      Aria2DM         │
                  │                      │
                  │  Native Desktop UI   │
                  │  URL Management      │
                  │  Download Controls   │
                  └──────────┬───────────┘
                             │
                       Provider Routing
                             │
             ┌───────────────┼───────────────┐
             │               │               │
        ┌────▼────┐    ┌─────▼─────┐   ┌─────▼────┐
        │  MEGA   │    │ Google    │   │ MediaFire│
        │ Resolver│    │ Drive     │   │ Resolver │
        └────┬────┘    └─────┬─────┘   └─────┬────┘
             │               │               │
             └───────────────┼───────────────┘
                             │
                  ┌──────────▼───────────┐
                  │     Aria2Client      │
                  │                      │
                  │ JSON-RPC Integration │
                  │ Download Monitoring  │
                  │ Queue Management     │
                  └──────────┬───────────┘
                             │
                  ┌──────────▼───────────┐
                  │     aria2 Engine     │
                  │                      │
                  │ HTTP / HTTPS         │
                  │ BitTorrent           │
                  │ Magnet               │
                  └──────────────────────┘
```

### Core Components

| Component | Responsibility |
|---|---|
| `App` | Application lifecycle and desktop window management |
| `AppUi` | User interface rendering |
| `AppInput` | User input and interaction |
| `Aria2` | aria2 engine integration and download operations |
| `BrowserHost` | Native Messaging and browser integration |
| `Canvas` | Application rendering infrastructure |
| `Json` | JSON parsing and serialization |
| `MegaProxy` | Local HTTP proxy for MEGA downloads |
| `Providers` | Provider detection and download URL resolution |
| `Theme` | UI theme definitions |
| `Util` | Shared Windows and application utilities |

---

## Technology Stack

Aria2DM is built using native Windows technologies.

| Technology | Purpose |
|---|---|
| C++17 | Core application implementation |
| Win32 API | Native Windows application |
| Direct2D | 2D graphics rendering |
| Direct3D 11 | Graphics infrastructure |
| DirectComposition | Composition infrastructure |
| DirectWrite | Text rendering |
| WinHTTP | HTTP and HTTPS communication |
| Winsock2 | Network communication |
| Windows Crypto API (CNG) | Cryptographic operations |
| Windows Registry | Native Messaging host registration |
| aria2 | Download engine |
| JSON-RPC | Download engine communication |

### Design Principles

- Native Windows implementation.
- Minimal external runtime dependencies.
- Asynchronous download operations.
- Separation between provider resolution and download execution.
- Browser communication through standard Native Messaging.
- Reuse of the aria2 download engine.

---

## Requirements

### Runtime

- Windows 10 or later.
- x64 architecture.
- Network connectivity.
- Compatible browser for browser integration.

### Development

To build Aria2DM from source:

- Visual Studio 2022 or compatible MSVC toolchain.
- Desktop development with C++ workload.
- Windows 10/11 SDK.
- MSVC v143 build tools.
- C++17 support.

---

## Building From Source

### 1. Clone the Repository

```powershell
git clone https://github.com/R3DIANCE/Aria2DM.git

cd Aria2DM
```

### 2. Open the Solution

Open:

```text
aria2dm.sln
```

Using Visual Studio 2022.

### 3. Select Build Configuration

Aria2DM provides the following configurations:

| Configuration | Platform |
|---|---|
| Debug | x64 |
| Release | x64 |

### 4. Build

Using Visual Studio:

```text
Build → Build Solution
```

Alternatively, use MSBuild:

```powershell
msbuild aria2dm.sln /p:Configuration=Release /p:Platform=x64
```

For a debug build:

```powershell
msbuild aria2dm.sln /p:Configuration=Debug /p:Platform=x64
```

### 5. Locate the Executable

Build output:

```text
build/
└── x64/
    ├── Debug/
    │   └── aria2dm.exe
    │
    └── Release/
        └── aria2dm.exe
```

---

## Repository Structure

```text
Aria2DM/
│
├── build/                    # Build output and intermediate files
│
├── res/                      # Application resources
│   ├── app.ico
│   ├── app.manifest
│   ├── app.rc
│   └── resource.h
│
├── src/                      # Application source code
│   ├── App.cpp
│   ├── App.h
│   ├── AppInput.cpp
│   ├── AppUi.cpp
│   ├── Aria2.cpp
│   ├── Aria2.h
│   ├── BrowserHost.cpp
│   ├── BrowserHost.h
│   ├── Canvas.cpp
│   ├── Canvas.h
│   ├── Json.cpp
│   ├── Json.h
│   ├── MegaProxy.cpp
│   ├── MegaProxy.h
│   ├── Providers.cpp
│   ├── Providers.h
│   ├── Theme.h
│   ├── UiIds.h
│   ├── Util.h
│   └── main.cpp
│
├── third_party/
│   └── aria2/                 # aria2-related third-party directory
│
├── tools/                     # Development utilities
│
├── aria2dm.sln                # Visual Studio solution
├── aria2dm.vcxproj            # Visual C++ project
└── README.md
```

---

## Command-Line Interface

Aria2DM supports several startup arguments.

| Argument | Description |
|---|---|
| `--add <URL>` | Submit a download URL |
| `--add=<URL>` | Submit a download URL using inline syntax |
| `--resolve <URL>` | Invoke the URL resolution startup path |
| `--background` | Start without showing the main window |
| `--native-host` | Run as a browser Native Messaging host |
| `--install-browser` | Install browser integration |

### Examples

Add a download:

```powershell
.\aria2dm.exe --add "https://example.com/file.zip"
```

Start in background mode:

```powershell
.\aria2dm.exe --background
```

Install browser integration:

```powershell
.\aria2dm.exe --install-browser
```

Resolve a provider URL:

```powershell
.\aria2dm.exe --resolve "https://example.com/file.zip"
```

Aria2DM also implements single-instance handling. Subsequent launches can forward download URLs to the existing application instead of opening additional application instances.

---

## Security and Privacy

Aria2DM uses Windows-native networking and inter-process communication.

Important security considerations:

- Browser communication is restricted through registered Native Messaging host manifests.
- Browser integration is registered under the current Windows user.
- The MEGA proxy uses a loopback address (`127.0.0.1`).
- Browser-submitted URLs are restricted to HTTP and HTTPS.
- Provider-specific cryptographic operations are handled by the application.

### Privacy

Download URLs may contain private access tokens, resource keys, or other sensitive information.

Users should avoid sharing private download links or MEGA decryption keys publicly.

Only download content you have permission to access.

---

## Troubleshooting

### Browser does not communicate with Aria2DM

1. Make sure Aria2DM is installed and executable.
2. Run the browser integration installer again.
3. Check that the generated Native Messaging manifest exists.
4. Verify that the browser extension is enabled.
5. Restart the browser.

### Downloads are not starting

1. Check your internet connection.
2. Verify that the URL is accessible.
3. Check whether the hosting provider requires authentication.
4. Verify that the download destination is writable.
5. Check the download's reported error status.

### MEGA links are not resolving

1. Verify that the MEGA link is complete.
2. Ensure the decryption key is included.
3. Use an individual file link rather than a folder link.
4. Check that the file is still accessible.

### Google Drive downloads fail

Some Google Drive files require account authentication, permission, or confirmation workflows. Not every shared file can be resolved as a direct download.

### MediaFire links are not resolving

MediaFire may change its download page structure or require additional verification. Such changes can affect the resolver.

---

## Contributing

Contributions, bug reports, and suggestions are welcome.

### Development Guidelines

- Keep the application native to Windows.
- Maintain compatibility with C++17.
- Follow the existing component separation.
- Avoid unnecessary third-party dependencies.
- Keep provider-specific logic inside the provider subsystem.
- Preserve the aria2 integration architecture.
- Test changes in both Debug and Release configurations.

### Reporting Issues

When submitting an issue, include:

- Windows version.
- Application build configuration.
- Browser and version, if applicable.
- Download provider.
- Steps to reproduce.
- Relevant error messages.

Never include private download URLs, authentication tokens, or decryption keys in public issue reports.

---

## Third-Party Software

Aria2DM incorporates or uses components associated with the following projects:

- [aria2](https://github.com/aria2/aria2) – Multi-protocol download utility.
- [Microsoft Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) – Windows development APIs.
- [Visual Studio](https://visualstudio.microsoft.com/) – C++ development toolchain.

Please refer to the respective projects for their licensing terms and attribution requirements.

---

## License

See the repository and included third-party license files for applicable licensing information.

---

## Links

- **Source Code:** https://github.com/R3DIANCE/Aria2DM
- **Issues:** https://github.com/R3DIANCE/Aria2DM/issues
- **aria2:** https://github.com/aria2/aria2
- **aria2 Documentation:** https://aria2.github.io/

---

<p align="center">
  <strong>Aria2DM</strong>
  <br/>
  Native downloading. Simplified.
  <br/><br/>
  Built with C++ and powered by aria2.
</p>
