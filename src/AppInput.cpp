#include "App.h"

#include "BrowserHost.h"
#include "UiIds.h"
#include "Util.h"

#include <algorithm>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>

namespace {

std::wstring ReadClipboard(HWND hwnd) {
    std::wstring result;
    if (!OpenClipboard(hwnd)) return result;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        auto* text = static_cast<const wchar_t*>(GlobalLock(handle));
        if (text) {
            result = text;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

void WriteClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* dest = GlobalLock(memory);
        if (dest) {
            memcpy(dest, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

Focus FocusForHit(int id) {
    if (id == UI_SearchField) return Focus::Search;
    if (id == UI_UrlField) return Focus::Url;
    if (id == UI_NameField) return Focus::Name;
    if (id == UI_MegaKeyField) return Focus::MegaKey;
    return Focus::None;
}

} // namespace

// ---------------------------------------------------------------------------
// TextInput

void TextInput::reset() {
    text.clear();
    caret = 0;
    anchor = 0;
    scroll = 0.0f;
}

void TextInput::setText(std::wstring value) {
    text = std::move(value);
    caret = text.size();
    anchor = caret;
    scroll = 0.0f;
}

void TextInput::selectAll() {
    anchor = 0;
    caret = text.size();
}

void TextInput::insert(const std::wstring& value) {
    std::wstring clean;
    clean.reserve(value.size());
    for (wchar_t c : value) clean.push_back(c < 32 ? L' ' : c);
    if (clean.empty()) return;
    auto sel = selection();
    text.replace(sel.first, sel.second - sel.first, clean);
    caret = sel.first + clean.size();
    anchor = caret;
}

void TextInput::backspace() {
    if (hasSelection()) {
        auto sel = selection();
        text.erase(sel.first, sel.second - sel.first);
        caret = anchor = sel.first;
    } else if (caret > 0) {
        text.erase(caret - 1, 1);
        --caret;
        anchor = caret;
    }
}

void TextInput::deleteForward() {
    if (hasSelection()) {
        auto sel = selection();
        text.erase(sel.first, sel.second - sel.first);
        caret = anchor = sel.first;
    } else if (caret < text.size()) {
        text.erase(caret, 1);
        anchor = caret;
    }
}

void TextInput::moveCaret(int delta, bool extend) {
    long long target = (long long)caret + delta;
    if (target < 0) target = 0;
    if (target > (long long)text.size()) target = (long long)text.size();
    caret = (size_t)target;
    if (!extend) anchor = caret;
}

void TextInput::moveHome(bool extend) {
    caret = 0;
    if (!extend) anchor = caret;
}

void TextInput::moveEnd(bool extend) {
    caret = text.size();
    if (!extend) anchor = caret;
}

float TextInput::caretXPrefix(Canvas& canvas, float size, size_t index) const {
    if (index == 0) return 0.0f;
    if (index >= text.size()) return canvas.textWidth(text, size);
    return canvas.textWidth(text.substr(0, index), size);
}

float TextInput::caretX(Canvas& canvas, float size) const {
    return caretXPrefix(canvas, size, caret);
}

size_t TextInput::indexFromX(Canvas& canvas, float localX, float size) const {
    if (text.empty()) return 0;
    float previous = 0.0f;
    for (size_t i = 1; i <= text.size(); ++i) {
        float width = caretXPrefix(canvas, size, i);
        if (width > localX) {
            float midpoint = (previous + width) * 0.5f;
            return localX < midpoint ? i - 1 : i;
        }
        previous = width;
    }
    return text.size();
}

// ---------------------------------------------------------------------------

TextInput* App::inputFor(Focus focus) {
    switch (focus) {
    case Focus::Search: return &searchInput_;
    case Focus::Url: return &urlInput_;
    case Focus::Name: return &nameInput_;
    case Focus::MegaKey: return &megaKeyInput_;
    default: return nullptr;
    }
}

void App::setFocus(Focus focus) {
    focus_ = focus;
    searchInput_.focused = focus == Focus::Search;
    urlInput_.focused = focus == Focus::Url;
    nameInput_.focused = focus == Focus::Name;
    megaKeyInput_.focused = focus == Focus::MegaKey;
    caretReset_ = util::NowMs();
    invalidate();
}

void App::updateHover(float x, float y) {
    const Hit* hit = hitTest(x, y);
    int id = hit ? hit->id : 0;
    std::wstring gid = hit ? hit->gid : std::wstring();
    if (id != hoverId_ || gid != hoverGid_) {
        hoverId_ = id;
        hoverGid_ = gid;
        invalidate();
    }
    hoverCursor_ = hit ? hit->cursor : nullptr;
    SetCursor(hoverCursor_ ? LoadCursorW(nullptr, hoverCursor_) : LoadCursorW(nullptr, IDC_ARROW));
}

void App::updateTextSelection(float x, float y) {
    (void)y;
    if (focus_ == Focus::None) return;
    int index = focusIndex(focus_);
    TextInput* input = inputFor(focus_);
    if (!input) return;
    const Rect& field = focus_ == Focus::MegaKey ? megaFieldTextRect_ : fieldTextRects_[index];
    float local = (x - field.x) + input->scroll;
    input->caret = input->indexFromX(canvas_, local, kFieldSize);
    caretReset_ = util::NowMs();
    invalidate();
}

void App::onMouseMove(int pixelX, int pixelY) {
    float x = (float)pixelX * 96.0f / dpi_;
    float y = (float)pixelY * 96.0f / dpi_;
    mouseX_ = x;
    mouseY_ = y;

    if (!trackingLeave_) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        TrackMouseEvent(&track);
        trackingLeave_ = true;
    }

    updateHover(x, y);
    if (mouseDown_ && draggingText_ && focus_ != Focus::None) {
        updateTextSelection(x, y);
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
    }
}

void App::onMouseDown(int pixelX, int pixelY) {
    float x = (float)pixelX * 96.0f / dpi_;
    float y = (float)pixelY * 96.0f / dpi_;
    mouseDown_ = true;

    const Hit* hit = hitTest(x, y);
    if (sortMenuOpen_) {
        bool onMenu = hit && (hit->id == UI_SortButton ||
                              (hit->id >= UI_SortOptionBase && hit->id <= UI_SortOptionLast));
        if (!onMenu) {
            closeSortMenu();
            hit = hitTest(x, y);
        }
    }
    if (!hit) {
        setFocus(Focus::None);
        pressedId_ = 0;
        pressedGid_.clear();
        invalidate();
        return;
    }

    Focus fieldFocus = FocusForHit(hit->id);
    if (fieldFocus != Focus::None) {
        setFocus(fieldFocus);
        TextInput* input = inputFor(fieldFocus);
        int index = focusIndex(fieldFocus);
        if (input) {
            const Rect& field =
                fieldFocus == Focus::MegaKey ? megaFieldTextRect_ : fieldTextRects_[index];
            float local = (x - field.x) + input->scroll;
            input->caret = input->indexFromX(canvas_, local, kFieldSize);
            input->anchor = input->caret;
        }
        caretReset_ = util::NowMs();
        draggingText_ = true;
        SetCapture(hwnd_);
        invalidate();
        return;
    }

    if (focus_ != Focus::None) setFocus(Focus::None);
    pressedId_ = hit->id;
    pressedGid_ = hit->gid;
    invalidate();
}

void App::onMouseUp(int pixelX, int pixelY) {
    float x = (float)pixelX * 96.0f / dpi_;
    float y = (float)pixelY * 96.0f / dpi_;
    mouseDown_ = false;

    if (draggingText_) {
        draggingText_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        invalidate();
        return;
    }

    int id = pressedId_;
    std::wstring gid = pressedGid_;
    pressedId_ = 0;
    pressedGid_.clear();

    const Hit* hit = hitTest(x, y);
    if (hit && hit->id == id && hit->gid == gid && id != 0) {
        executeAction(id, gid);
    }
    invalidate();
}

void App::onMouseDoubleClick(int pixelX, int pixelY) {
    float x = (float)pixelX * 96.0f / dpi_;
    float y = (float)pixelY * 96.0f / dpi_;
    const Hit* hit = hitTest(x, y);
    if (!hit) return;

    Focus fieldFocus = FocusForHit(hit->id);
    if (fieldFocus != Focus::None) {
        TextInput* input = inputFor(fieldFocus);
        if (input) {
            input->selectAll();
            caretReset_ = util::NowMs();
            invalidate();
        }
        return;
    }

    if (hit->id == UI_Card) {
        const Aria2Item* item = findItem(hit->gid);
        if (item) {
            if (!item->path.empty() && util::FileExists(item->path))
                util::RevealInExplorer(item->path);
            else
                util::OpenFolder(item->dir.empty() ? settings_.downloadDir : item->dir);
        }
    }
}

void App::onMouseWheel(int pixelX, int pixelY, int delta) {
    float x = (float)pixelX * 96.0f / dpi_;
    float y = (float)pixelY * 96.0f / dpi_;
    if (page_ == Page::Settings) {
        if (!settingsRect_.contains(x, y)) return;
        float maxScroll = std::max(0.0f, settingsContentHeight_ - settingsRect_.h);
        scroll_ -= ((float)delta / 120.0f) * 72.0f;
        scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
        invalidate();
        return;
    }
    if (!listRect_.contains(x, y)) return;
    float maxScroll = std::max(0.0f, contentHeight_ - listRect_.h);
    scroll_ -= ((float)delta / 120.0f) * 72.0f;
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
    invalidate();
}

void App::onKeyDown(WPARAM key) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (megaPromptOpen_) {
        if (key == VK_ESCAPE) {
            closeMegaPrompt();
            return;
        }
        if (key == VK_RETURN) {
            submitMegaKey();
            return;
        }
    }

    if (dialogOpen_) {
        if (key == VK_ESCAPE) {
            closeAddDialog();
            return;
        }
        if (key == VK_RETURN) {
            submitAdd();
            return;
        }
        if (key == VK_TAB) {
            Focus next;
            if (focus_ == Focus::Url)
                next = Focus::Name;
            else
                next = Focus::Url;
            setFocus(next);
            return;
        }
    } else {
        if (sortMenuOpen_ && key == VK_ESCAPE) {
            closeSortMenu();
            return;
        }
        if (ctrl && key == 'F') {
            setFocus(Focus::Search);
            searchInput_.selectAll();
            caretReset_ = util::NowMs();
            return;
        }
        if (key == VK_ESCAPE) {
            if (focus_ != Focus::None) {
                setFocus(Focus::None);
            } else if (!searchInput_.text.empty()) {
                searchInput_.reset();
                focus_ = Focus::None;
                invalidate();
            }
            return;
        }
        if (key == VK_RETURN) return;
    }

    if (focus_ == Focus::None) return;
    TextInput* input = inputFor(focus_);
    if (input) handleEditingKey(*input, key, ctrl, shift);
}

bool App::handleEditingKey(TextInput& input, WPARAM key, bool ctrl, bool shift) {
    bool changed = false;
    if (ctrl) {
        switch (key) {
        case 'A':
            input.selectAll();
            changed = true;
            break;
        case 'C':
            copySelection(input, false);
            break;
        case 'X':
            copySelection(input, true);
            changed = true;
            break;
        case 'V': {
            std::wstring clip = ReadClipboard(hwnd_);
            if (!clip.empty()) {
                input.insert(clip);
                changed = true;
            }
            break;
        }
        default:
            break;
        }
    } else {
        switch (key) {
        case VK_BACK:
            input.backspace();
            changed = true;
            break;
        case VK_DELETE:
            input.deleteForward();
            changed = true;
            break;
        case VK_LEFT:
            input.moveCaret(-1, shift);
            changed = true;
            break;
        case VK_RIGHT:
            input.moveCaret(1, shift);
            changed = true;
            break;
        case VK_HOME:
            input.moveHome(shift);
            changed = true;
            break;
        case VK_END:
            input.moveEnd(shift);
            changed = true;
            break;
        default:
            break;
        }
    }
    if (changed) {
        caretReset_ = util::NowMs();
        invalidate();
    }
    return changed;
}

void App::copySelection(TextInput& input, bool cut) {
    if (!input.hasSelection()) return;
    auto sel = input.selection();
    std::wstring selected = input.text.substr(sel.first, sel.second - sel.first);
    WriteClipboard(hwnd_, selected);
    if (cut) {
        input.text.erase(sel.first, sel.second - sel.first);
        input.caret = input.anchor = sel.first;
        caretReset_ = util::NowMs();
        invalidate();
    }
}

void App::onChar(wchar_t ch) {
    if (ch < 32 || ch == 127) return;
    Focus target = focus_;
    if (dialogOpen_) {
        if (target == Focus::None) target = Focus::Url;
    } else if (target == Focus::None && page_ != Page::Settings) {
        target = Focus::Search;
    }
    if (target == Focus::None) return;
    if (focus_ != target) setFocus(target);
    TextInput* input = inputFor(target);
    if (!input) return;
    std::wstring text(1, ch);
    input->insert(text);
    caretReset_ = util::NowMs();
    invalidate();
}

void App::openAddDialog() {
    dialogOpen_ = true;
    dialogDir_ = settings_.downloadDir;
    addError_.clear();
    urlInput_.reset();
    nameInput_.reset();
    hoverId_ = 0;
    hoverGid_.clear();
    setFocus(Focus::Url);
    invalidate();
}

void App::closeAddDialog() {
    dialogOpen_ = false;
    addError_.clear();
    focus_ = Focus::None;
    searchInput_.focused = urlInput_.focused = nameInput_.focused = false;
    invalidate();
}

void App::submitAdd() {
    std::wstring url = util::Trim(urlInput_.text);
    if (url.empty()) {
        addError_ = L"Enter a link or magnet URI.";
        invalidate();
        return;
    }
    bool valid = url.find(L"://") != std::wstring::npos || url.rfind(L"magnet:", 0) == 0 ||
                 util::FileExists(url);
    if (!valid) {
        addError_ = L"That does not look like a download link.";
        invalidate();
        return;
    }

    // Never fetch the same link twice once it has finished successfully.
    for (const Aria2Item& item : uiSnapshot_.items) {
        if (item.isComplete() && !item.uri.empty() && util::Lower(item.uri) == util::Lower(url)) {
            addError_ = L"This link has already been downloaded.";
            invalidate();
            return;
        }
    }

    aria_.addUri(url, dialogDir_, util::Trim(nameInput_.text));
    closeAddDialog();
    if (page_ == Page::Settings) page_ = Page::All;
    showToast(L"Download added");
    scroll_ = 0.0f;
}

// ---------------------------------------------------------------------------
// Browser / command-line link ingestion

void App::ingestUrl(const std::wstring& url, const std::wstring& key) {
    std::wstring trimmed = util::Trim(url);
    if (trimmed.empty()) return;
    if (trimmed.rfind(L"magnet:", 0) != 0 && trimmed.find(L"://") == std::wstring::npos &&
        !util::FileExists(trimmed)) {
        showToast(L"That does not look like a download link.");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(resolveMutex_);
        resolveQueue_.push_back(ResolveJob{trimmed, key});
    }
    resolveCv_.notify_one();
}

void App::startResolveWorker() {
    if (resolveThread_.joinable()) return;
    resolveThread_ = std::thread([this] { resolveLoop(); });
}

void App::stopResolveWorker() {
    {
        std::lock_guard<std::mutex> lock(resolveMutex_);
        resolveQuit_ = true;
    }
    resolveCv_.notify_all();
    if (resolveThread_.joinable()) resolveThread_.join();
}

void App::resolveLoop() {
    for (;;) {
        ResolveJob job;
        {
            std::unique_lock<std::mutex> lock(resolveMutex_);
            resolveCv_.wait(lock, [this] { return resolveQuit_ || !resolveQueue_.empty(); });
            if (resolveQuit_) break;
            job = resolveQueue_.front();
            resolveQueue_.pop_front();
        }

        ResolvedLink link = ResolveLink(job.url, job.key);
        {
            std::lock_guard<std::mutex> lock(resolveMutex_);
            resolveResults_.push_back(ResolveOutcome{job.url, job.key, link});
        }
        if (!resolvePosted_.exchange(true)) {
            if (!hwnd_ || !PostMessageW(hwnd_, WM_APP_INGEST, 0, 0)) resolvePosted_ = false;
        }
    }
}

void App::onIngestMessage() {
    resolvePosted_ = false;
    std::vector<ResolveOutcome> outcomes;
    {
        std::lock_guard<std::mutex> lock(resolveMutex_);
        outcomes.swap(resolveResults_);
    }

    for (const ResolveOutcome& outcome : outcomes) {
        const ResolvedLink& link = outcome.link;
        if (link.needsKey) {
            openMegaPrompt(outcome.source);
            if (!link.error.empty()) megaPromptError_ = link.error;
            continue;
        }
        if (!link.error.empty()) {
            showToast(link.error);
            continue;
        }
        addResolved(link);
    }
}

void App::addResolved(const ResolvedLink& link) {
    std::wstring url = link.url;
    bool singleConnection = false;

    if (link.provider == Provider::Mega) {
        if (link.megaRemote.empty() || link.megaKey.size() != 32) {
            showToast(L"The MEGA link could not be resolved.");
            return;
        }
        std::wstring proxyError;
        if (!megaProxy_.start(&proxyError)) {
            showToast(proxyError.empty() ? L"Could not start the MEGA proxy." : proxyError);
            return;
        }
        url = megaProxy_.addFile(link.megaRemote, link.megaKey, link.size, link.filename);
        if (url.empty()) {
            showToast(L"Could not prepare the MEGA download.");
            return;
        }
        singleConnection = true;
    }

    if (url.empty()) return;

    for (const Aria2Item& item : uiSnapshot_.items) {
        if (!item.uri.empty() && util::Lower(item.uri) == util::Lower(url) && !item.isError() &&
            !item.isRemoved()) {
            showToast(L"That link is already in the download list.");
            return;
        }
    }

    if (uiSnapshot_.state != EngineState::Connected) {
        pendingAdds_.push_back(PendingAdd{url, link.filename, singleConnection});
        showToast(L"Download queued until the engine is ready.");
        return;
    }

    aria_.addUri(url, settings_.downloadDir, link.filename, singleConnection);
    if (page_ == Page::Settings) page_ = Page::All;
    scroll_ = 0.0f;
    showToast(L"Download added");
    invalidate();
}

void App::flushPendingAdds() {
    if (pendingAdds_.empty()) return;
    std::vector<PendingAdd> queued;
    queued.swap(pendingAdds_);
    for (const PendingAdd& add : queued)
        aria_.addUri(add.url, settings_.downloadDir, add.out, add.singleConnection);
}

void App::openMegaPrompt(const std::wstring& url) {
    megaPromptOpen_ = true;
    megaPromptUrl_ = url;
    megaPromptError_.clear();
    megaKeyInput_.reset();
    mouseDown_ = false;
    draggingText_ = false;
    if (hwnd_ && (!IsWindowVisible(hwnd_) || IsIconic(hwnd_))) showFromTray();
    setFocus(Focus::MegaKey);
    invalidate();
}

void App::closeMegaPrompt() {
    megaPromptOpen_ = false;
    megaPromptError_.clear();
    megaKeyInput_.focused = false;
    focus_ = Focus::None;
    invalidate();
}

void App::submitMegaKey() {
    std::wstring key = util::Trim(megaKeyInput_.text);
    if (key.empty()) {
        megaPromptError_ = L"Paste the decryption key or the full MEGA link.";
        invalidate();
        return;
    }
    const std::wstring url = megaPromptUrl_;
    closeMegaPrompt();
    ingestUrl(url, key);
    showToast(L"Checking the MEGA key\u2026");
}

void App::installBrowserIntegration() {
    std::wstring message;
    const bool ok = ::InstallBrowserIntegration(dataDir_, message);
    showToast(message);
    if (ok) util::OpenFolder(dataDir_ + L"\\browser");
}

void App::browseForFolder(std::wstring& target) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return;
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose a folder");
    if (SUCCEEDED(dialog->Show(hwnd_))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                target = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}

void App::setDownloadDir(const std::wstring& dir) {
    if (dir.empty() || dir == settings_.downloadDir) return;
    settings_.downloadDir = dir;
    util::EnsureDir(settings_.downloadDir);
    saveSettings();
    applyEngineOptions();
    invalidate();
}

void App::applyEngineOptions() {
    aria_.applyOptions(settings_.maxConcurrent, settings_.speedLimit, settings_.downloadDir);
}

void App::executeAction(int id, const std::wstring& gid) {
    if (id >= UI_SortOptionBase && id <= UI_SortOptionLast) {
        setSortMode((SortMode)(id - UI_SortOptionBase));
        return;
    }
    if (id >= UI_NavAll && id <= UI_NavSettings) {
        page_ = (Page)(id - UI_NavAll);
        scroll_ = 0.0f;
        sortMenuOpen_ = false;
        setFocus(Focus::None);
        invalidate();
        return;
    }

    switch (id) {
    case UI_NewDownload:
        openAddDialog();
        break;
    case UI_InstallBrowser:
        installBrowserIntegration();
        break;
    case UI_PaneToggle:
        toggleSidebar();
        break;
    case UI_SortButton:
        sortMenuOpen_ = !sortMenuOpen_;
        invalidate();
        break;
    case UI_ClearCompleted:
        aria_.clearCompleted();
        break;
    case UI_CaptionMin:
        ShowWindow(hwnd_, SW_MINIMIZE);
        break;
    case UI_CaptionMax:
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
        break;
    case UI_CaptionClose:
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    case UI_ActionPause: {
        const Aria2Item* item = findItem(gid);
        if (item) aria_.pauseItem(gid, true);
        break;
    }
    case UI_ActionResume: {
        const Aria2Item* item = findItem(gid);
        if (item) aria_.pauseItem(gid, false);
        break;
    }
    case UI_ActionFolder: {
        const Aria2Item* item = findItem(gid);
        if (!item) break;
        if (!item->path.empty() && util::FileExists(item->path))
            util::RevealInExplorer(item->path);
        else
            util::OpenFolder(item->dir.empty() ? settings_.downloadDir : item->dir);
        break;
    }
    case UI_ActionRemove: {
        const Aria2Item* item = findItem(gid);
        if (item) aria_.removeItem(gid, item->status);
        break;
    }
    case UI_ActionRetry: {
        const Aria2Item* item = findItem(gid);
        if (item) aria_.retryItem(gid, item->status, item->uri, item->dir, item->name, item->isTorrent);
        break;
    }
    case UI_ActionCopyLink: {
        const Aria2Item* item = findItem(gid);
        if (!item) break;
        std::wstring link = !item->uri.empty() ? item->uri : item->path;
        if (link.empty()) break;
        WriteClipboard(hwnd_, link);
        showToast(L"Download link copied");
        break;
    }
    case UI_RetryEngine:
        aria_.retryEngine();
        showToast(L"Restarting the aria2 engine\u2026");
        break;
    case UI_BrowseDir: {
        std::wstring dir = settings_.downloadDir;
        browseForFolder(dir);
        setDownloadDir(dir);
        break;
    }
    case UI_OpenData:
        util::OpenFolder(dataDir_);
        break;
    case UI_OpenDownloads:
        util::OpenFolder(settings_.downloadDir);
        break;
    case UI_MaxMinus:
        if (settings_.maxConcurrent > 1) {
            --settings_.maxConcurrent;
            saveSettings();
            applyEngineOptions();
            invalidate();
        }
        break;
    case UI_MaxPlus:
        if (settings_.maxConcurrent < 16) {
            ++settings_.maxConcurrent;
            saveSettings();
            applyEngineOptions();
            invalidate();
        }
        break;
    case UI_LimitMinus:
        if (settings_.speedLimit > 0) {
            settings_.speedLimit = std::max(0LL, settings_.speedLimit - 1024LL * 1024);
            saveSettings();
            applyEngineOptions();
            invalidate();
        }
        break;
    case UI_LimitPlus:
        if (settings_.speedLimit < 1024LL * 1024 * 1024) {
            settings_.speedLimit += 1024LL * 1024;
            saveSettings();
            applyEngineOptions();
            invalidate();
        }
        break;
    case UI_ToggleStartup:
        settings_.startWithWindows = !settings_.startWithWindows;
        saveSettings();
        applyStartupSetting();
        invalidate();
        break;
    case UI_DialogClose:
    case UI_DialogCancel:
        closeAddDialog();
        break;
    case UI_MegaClose:
    case UI_MegaCancel:
        closeMegaPrompt();
        break;
    case UI_MegaSubmit:
        submitMegaKey();
        break;
    case UI_DialogSubmit:
        submitAdd();
        break;
    case UI_DialogBrowse:
        browseForFolder(dialogDir_);
        invalidate();
        break;
    default:
        break;
    }
}
