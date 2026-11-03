#pragma once

#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include "Aria2.h"
#include "Canvas.h"
#include "MegaProxy.h"
#include "Providers.h"
#include "Theme.h"

// Shared by App.cpp and main.cpp so the single-instance search finds the same
// window class the running copy registered.
inline constexpr wchar_t kAppWindowClass[] = L"Aria2DMWindow";
inline constexpr wchar_t kAppInstanceMutex[] = L"Local\\Aria2DM.SingleInstance";

enum class Page { All = 0, Active, Waiting, Paused, Completed, Settings, Count };
enum class Focus { None = -1, Search = 0, Url = 1, Name = 2, MegaKey = 3 };
enum class SortMode { Added = 0, Name, Progress, Size, Status, Count };

struct Settings {
    std::wstring downloadDir;
    int maxConcurrent = 5;
    long long speedLimit = 0;
    bool startWithWindows = true;
    bool sidebarCollapsed = false;
};

struct TextInput {
    std::wstring text;
    size_t caret = 0;
    size_t anchor = 0;
    float scroll = 0;
    bool focused = false;

    void reset();
    void setText(std::wstring value);
    bool hasSelection() const { return caret != anchor; }
    std::pair<size_t, size_t> selection() const {
        return caret < anchor ? std::make_pair(caret, anchor) : std::make_pair(anchor, caret);
    }
    void selectAll();
    void insert(const std::wstring& value);
    void backspace();
    void deleteForward();
    void moveCaret(int delta, bool extend);
    void moveHome(bool extend);
    void moveEnd(bool extend);
    size_t indexFromX(Canvas& canvas, float localX, float size) const;
    float caretX(Canvas& canvas, float size) const;
    float caretXPrefix(Canvas& canvas, float size, size_t index) const;
};

struct Hit {
    int id = 0;
    std::wstring gid;
    int index = -1;
    Rect rect;
    LPCWSTR cursor = nullptr;
};

// Column geometry for the compact download table. Optional columns disappear
// as the window narrows so the name column never gets squeezed out.
struct RowLayout {
    bool showSize = true;
    bool showProgress = true;
    bool showSpeed = true;
    bool showStatus = true;
    bool showAdded = true;
    bool showDone = true;
    float nameX = 0.0f;
    float nameW = 0.0f;
    float sizeX = 0.0f;
    float sizeW = 0.0f;
    float progressX = 0.0f;
    float progressW = 0.0f;
    float speedX = 0.0f;
    float speedW = 0.0f;
    float statusX = 0.0f;
    float statusW = 0.0f;
    float addedX = 0.0f;
    float addedW = 0.0f;
    float doneX = 0.0f;
    float doneW = 0.0f;
    float actionsX = 0.0f;
};

// Rolling download-speed history, drawn as the bar chart behind each row.
struct SpeedHistory {
    std::deque<float> samples;
    uint64_t lastSample = 0;
    float peak = 65536.0f;
};

struct ItemTimes {
    std::time_t added = 0;
    std::time_t completed = 0;
};

void AppLog(const std::wstring& folder, const std::wstring& text);

struct ResolveJob {
    std::wstring url;
    std::wstring key;
};

struct ResolveOutcome {
    std::wstring source;
    std::wstring key;
    ResolvedLink link;
};

struct PendingAdd {
    std::wstring url;
    std::wstring out;
    bool singleConnection = false;
};

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    bool create(int showCommand);
    int run();

    // A link handed to this process on the command line or by the browser
    // native host. It is resolved on a worker thread once the window exists.
    void queueStartupUrl(const std::wstring& url);
    void setStartHidden(bool hidden) { startHidden_ = hidden; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void onPaint();
    void onResize(UINT pixelWidth, UINT pixelHeight);
    void onDpiChanged(UINT dpi, RECT* suggested);
    void centerInWorkArea();
    void onMouseMove(int pixelX, int pixelY);
    void onMouseDown(int pixelX, int pixelY);
    void onMouseUp(int pixelX, int pixelY);
    void onMouseDoubleClick(int pixelX, int pixelY);
    void onMouseWheel(int pixelX, int pixelY, int delta);
    void onKeyDown(WPARAM key);
    void onChar(wchar_t ch);
    void onTimer();
    void onSnapshotMessage();
    void onToastMessage();

    void render();
    void toggleSidebar();
    void stepSidebarAnimation();
    void renderSortMenu();
    void drawSortButton();
    void drawTableHeader();
    void computeTableLayout(int actionCount);
    void sampleSpeeds();
    void updateItemTimes();
    std::wstring sortLabel() const;
    void setSortMode(SortMode mode);
    void closeSortMenu();
    float sidebarWidth() const { return sidebarWidth_; }
    void renderTitleBar();
    void renderSidebar();
    void renderDownloads();
    void renderSettings();
    void renderStatusBar();
    void renderDialog();
    void renderToast();

    void drawCard(const Aria2Item& item, const Rect& rect);
    void drawProgressFill(const Rect& rect, const Aria2Item& item);
    void drawNavItem(Page page, const std::wstring& label, wchar_t glyph, int count, float y);
    void drawIconButton(int id, const Rect& rect, wchar_t glyph, Color color, Color hoverBg,
                        const std::wstring& gid = {});
    void drawTextField(int id, Focus focus, const Rect& rect, TextInput& input,
                       const std::wstring& placeholder, wchar_t icon);
    void drawStepper(const Rect& rect, const std::wstring& value, int minusId, int plusId,
                     bool canMinus, bool canPlus);
    void drawToggleSwitch(int id, const Rect& rect, bool on);
    void drawPrimaryButton(int id, const Rect& rect, const std::wstring& label, bool enabled);
    void drawGhostButton(int id, const Rect& rect, const std::wstring& label);
    void drawPill(const Rect& rect, const std::wstring& label, Color dotColor, Color textColor);
    void drawCenteredEmptyState(const Rect& area);

    std::vector<const Aria2Item*> filteredItems() const;
    const Aria2Item* findItem(const std::wstring& gid) const;
    bool isHovered(int id, const std::wstring& gid = {}) const;
    bool isPressed(int id, const std::wstring& gid = {}) const;
    void addHit(int id, const Rect& rect, const std::wstring& gid = {}, int index = -1,
                LPCWSTR cursor = nullptr);
    const Hit* hitTest(float x, float y) const;
    void invalidate();
    void applyTheme();
    void showToast(const std::wstring& text);
    void openAddDialog();
    void closeAddDialog();
    void submitAdd();
    void ingestUrl(const std::wstring& url, const std::wstring& key = {});
    void onIngestMessage();
    void startResolveWorker();
    void stopResolveWorker();
    void resolveLoop();
    void flushPendingAdds();
    void addResolved(const ResolvedLink& link);
    void openMegaPrompt(const std::wstring& url);
    void closeMegaPrompt();
    void submitMegaKey();
    void installBrowserIntegration();
    void renderMegaPrompt();
    void setFocus(Focus focus);
    TextInput* inputFor(Focus focus);
    static int focusIndex(Focus focus) { return (int)focus; }
    float scale() const { return dpi_ / 96.0f; }
    void saveSettings();
    void loadSettings();
    void applyStartupSetting();
    bool addTrayIcon();
    void removeTrayIcon();
    NOTIFYICONDATAW trayData() const;
    void showTrayMenu();
    void hideToTray(bool notify = true);
    void showFromTray();
    void requestExit();
    void browseForFolder(std::wstring& target);
    void executeAction(int id, const std::wstring& gid);
    void setDownloadDir(const std::wstring& dir);
    void applyEngineOptions();
    std::wstring pageTitle() const;
    std::wstring pageSubtitle() const;
    int countForPage(Page page) const;
    void updateHover(float x, float y);
    void updateTextSelection(float x, float y);
    bool handleEditingKey(TextInput& input, WPARAM key, bool ctrl, bool shift);
    void copySelection(TextInput& input, bool cut);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    Canvas canvas_;
    Colors colors_ = DarkColors(false, 0xFF4CC2FFu);
    bool dark_ = true;
    bool mica_ = false;
    float dpi_ = 96.0f;
    float width_ = 0.0f;
    float height_ = 0.0f;

    Settings settings_;
    std::wstring dataDir_;
    std::wstring settingsPath_;
    bool exiting_ = false;
    bool startHidden_ = false;
    std::wstring startupUrl_;
    bool trayAdded_ = false;
    bool trayHintShown_ = false;
    UINT taskbarCreated_ = 0;

    Aria2Client aria_;
    std::mutex snapshotMutex_;
    Aria2Snapshot snapshot_;
    Aria2Snapshot uiSnapshot_;
    std::atomic<bool> snapshotPosted_{false};
    std::mutex toastMutex_;
    std::wstring pendingToast_;
    std::atomic<bool> toastPosted_{false};

    Page page_ = Page::All;
    Focus focus_ = Focus::None;
    TextInput searchInput_;
    TextInput urlInput_;
    TextInput nameInput_;
    bool dialogOpen_ = false;
    std::wstring dialogDir_;
    std::wstring addError_;
    std::wstring toast_;
    uint64_t toastUntil_ = 0;
    uint64_t caretReset_ = 0;
    float scroll_ = 0.0f;
    float contentHeight_ = 0.0f;
    bool sidebarCollapsed_ = false;
    bool sidebarAnimating_ = false;
    float sidebarWidth_ = 228.0f;
    SortMode sortMode_ = SortMode::Added;
    bool sortMenuOpen_ = false;
    Rect sortButtonRect_;
    RowLayout tableLayout_;
    std::map<std::wstring, SpeedHistory> speedHistory_;
    std::map<std::wstring, ItemTimes> itemTimes_;

    // Link resolution runs on its own worker so MediaFire scraping and MEGA
    // API calls never block the UI thread.
    std::thread resolveThread_;
    std::mutex resolveMutex_;
    std::condition_variable resolveCv_;
    std::deque<ResolveJob> resolveQueue_;
    std::vector<ResolveOutcome> resolveResults_;
    bool resolveQuit_ = false;
    std::atomic<bool> resolvePosted_{false};
    std::vector<PendingAdd> pendingAdds_;

    MegaProxy megaProxy_;
    bool megaPromptOpen_ = false;
    std::wstring megaPromptUrl_;
    std::wstring megaPromptError_;
    TextInput megaKeyInput_;
    Rect megaDialogRect_;
    Rect megaFieldRect_;
    Rect megaFieldTextRect_;

    std::vector<Hit> hits_;
    int hoverId_ = 0;
    std::wstring hoverGid_;
    LPCWSTR hoverCursor_ = nullptr;
    int pressedId_ = 0;
    std::wstring pressedGid_;
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    bool mouseDown_ = false;
    bool draggingText_ = false;
    bool trackingLeave_ = false;

    Rect searchRect_;
    Rect searchTextRect_;
    Rect listRect_;
    Rect settingsRect_;
    float settingsContentHeight_ = 0.0f;
    Rect statusBarRect_;
    Rect contentRect_;
    Rect dialogRect_;
    Rect fieldRects_[3];
    Rect fieldTextRects_[3];
    Rect captionMinRect_;
    Rect captionMaxRect_;
    Rect captionCloseRect_;

    static constexpr int kTitleHeight = 48;
    static constexpr int kSidebarWidth = 228;
    static constexpr int kSidebarCompactWidth = 52;
    static constexpr int kStatusHeight = 38;
    static constexpr float kFieldSize = 13.0f;
};
