#include "App.h"

#include "UiIds.h"
#include "Util.h"

#include <algorithm>
#include <cmath>

namespace {

Color Tint(Color color, uint32_t alpha) {
    return (color & 0x00FFFFFF) | (alpha << 24);
}

// Small extension badge shown at the start of each row. The exact extension is
// kept when it is short enough, so "mkv" and "mp4" remain distinguishable.
struct TypeBadge {
    std::wstring label;
    wchar_t glyph = 0;
    Color color = 0xFF9E9E9Eu;
};

TypeBadge BadgeFor(const std::wstring& name, bool torrent) {
    if (torrent) return {L"TOR", 0, 0xFF6CCB5Fu};

    std::wstring ext;
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot + 1 < name.size()) {
        ext = util::Lower(name.substr(dot + 1));
        if (ext.size() > 4) ext.resize(4);
    }
    if (ext.empty()) return {L"", 0xE8A5, 0xFF9E9E9Eu}; // generic document

    auto any = [&](std::initializer_list<const wchar_t*> list) {
        for (const wchar_t* value : list)
            if (ext == value) return true;
        return false;
    };

    Color color = 0xFF9E9E9Eu;
    if (any({L"mp4", L"mkv", L"avi", L"mov", L"webm", L"m4v", L"flv", L"wmv", L"mpg", L"mpeg",
             L"ts"}))
        color = 0xFF8E7CFFu;
    else if (any({L"mp3", L"flac", L"wav", L"aac", L"ogg", L"m4a", L"opus", L"wma"}))
        color = 0xFFF07AC0u;
    else if (any({L"jpg", L"jpeg", L"png", L"gif", L"webp", L"bmp", L"svg", L"avif", L"heic"}))
        color = 0xFF5CC8A0u;
    else if (any({L"zip", L"rar", L"7z", L"tar", L"gz", L"bz2", L"xz", L"zst"}))
        color = 0xFFFFB35Cu;
    else if (any({L"pdf", L"doc", L"docx", L"xls", L"xlsx", L"ppt", L"pptx", L"txt", L"epub",
                  L"rtf", L"csv"}))
        color = 0xFF6CB6FFu;
    else if (any({L"exe", L"msi", L"apk", L"dmg", L"pkg", L"deb", L"rpm"}))
        color = 0xFFFF8A65u;
    else if (any({L"iso", L"img", L"bin", L"vhd", L"vmdk"}))
        color = 0xFFB0B0B0u;
    else if (any({L"json", L"xml", L"html", L"htm", L"js", L"css", L"py", L"cpp", L"h"}))
        color = 0xFF7CD3E0u;
    else if (any({L"srt", L"ass", L"vtt", L"sub"}))
        color = 0xFF9AA7FFu;

    std::wstring label = ext;
    for (wchar_t& ch : label)
        if (ch >= L'a' && ch <= L'z') ch = (wchar_t)(ch - L'a' + L'A');
    return {label, 0, color};
}

void DrawTypeBadge(Canvas& canvas, const Rect& rect, const std::wstring& name, bool torrent) {
    const TypeBadge badge = BadgeFor(name, torrent);
    canvas.fillRound(rect, 6, Tint(badge.color, 0x2A));
    canvas.strokeRound(rect.inset(0.5f), 6, Tint(badge.color, 0x55), 1.0f);
    if (badge.label.empty()) {
        canvas.glyph(rect, badge.glyph, 13, badge.color);
    } else {
        canvas.text(rect, badge.label, badge.label.size() >= 4 ? 8.0f : 9.0f, badge.color,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

Color StatusColor(const Colors& colors, const Aria2Item& item) {
    if (item.isActive()) return colors.accent;
    if (item.isPaused()) return colors.warning;
    if (item.isComplete()) return colors.success;
    if (item.isError()) return colors.danger;
    if (item.isRemoved()) return colors.textFaint;
    return colors.textDim;
}

wchar_t StatusGlyph(const Aria2Item& item) {
    if (item.isActive()) return 0xE896; // Download
    if (item.isPaused()) return 0xE769; // Pause
    if (item.isComplete()) return 0xE73E; // CheckMark
    if (item.isError()) return 0xE783; // Error
    if (item.isRemoved()) return 0xE74D; // Delete
    return 0xE823; // Recent / clock
}

std::wstring PercentText(const Aria2Item& item) {
    int percent = (int)std::lround(item.progress() * 100.0);
    if (item.isComplete()) percent = 100;
    return std::to_wstring(percent) + L"%";
}

// Grouping used by the "Status" sort mode.
int StatusRank(const Aria2Item& item) {
    if (item.isActive()) return 0;
    if (item.isWaiting()) return 1;
    if (item.isPaused()) return 2;
    if (item.isComplete()) return 3;
    if (item.isError()) return 4;
    return 5;
}

std::wstring StatusText(const Aria2Item& item) {
    if (item.isActive()) return L"Downloading";
    if (item.isWaiting()) return L"Queued";
    if (item.isPaused()) return L"Paused";
    if (item.isComplete()) return L"Completed";
    if (item.isError()) return L"Failed";
    if (item.isRemoved()) return L"Removed";
    return L"Idle";
}

// aria2 reports bare protocol text such as
// "The response status is not successful, status=403". Translate the common
// cases into something a person can act on.
std::wstring FriendlyError(const std::wstring& raw) {
    if (raw.empty()) return L"Download failed";
    size_t pos = raw.find(L"status=");
    if (pos != std::wstring::npos) {
        int status = 0;
        for (const wchar_t* p = raw.c_str() + pos + 7; *p >= L'0' && *p <= L'9'; ++p)
            status = status * 10 + (*p - L'0');
        switch (status) {
        case 401:
        case 407:
            return L"Authentication is required by the server (" + std::to_wstring(status) + L").";
        case 403:
            return L"The server refused the request (403 Forbidden). The link may be "
                   L"expired, private, or above the server's size limit.";
        case 404:
            return L"The file was not found on the server (404).";
        case 429:
            return L"The server is rate limiting requests (429). Try again later.";
        case 500:
        case 502:
        case 503:
        case 504:
            return L"The server had a problem (" + std::to_wstring(status) + L"). Try again later.";
        default:
            return L"The server returned HTTP " + std::to_wstring(status) + L".";
        }
    }
    if (raw.find(L"Timeout") != std::wstring::npos || raw.find(L"timeout") != std::wstring::npos)
        return L"The connection timed out.";
    if (raw.find(L"unreachable network") != std::wstring::npos ||
        raw.find(L"Network is unreachable") != std::wstring::npos) {
        return L"The server could not be reached over the current network. This usually means "
               L"IPv6 is enabled without a working route; retrying over IPv4 should fix it.";
    }
    if (raw.find(L"Name resolution") != std::wstring::npos)
        return L"Could not resolve the server name. Check the link and your connection.";
    if (raw.find(L"SSL") != std::wstring::npos || raw.find(L"certificate") != std::wstring::npos)
        return L"The secure connection could not be verified (TLS certificate).";
    return raw;
}

} // namespace

std::vector<const Aria2Item*> App::filteredItems() const {
    std::vector<const Aria2Item*> result;
    const std::wstring& query = searchInput_.text;
    for (const Aria2Item& item : uiSnapshot_.items) {
        if (item.isMetadataPlaceholder()) continue;
        bool matchesPage = false;
        switch (page_) {
        case Page::All: matchesPage = true; break;
        case Page::Active: matchesPage = item.isActive(); break;
        case Page::Waiting: matchesPage = item.isWaiting(); break;
        case Page::Paused: matchesPage = item.isPaused(); break;
        case Page::Completed: matchesPage = item.isFinished(); break;
        default: break;
        }
        if (!matchesPage) continue;
        if (!query.empty() && !util::ContainsNoCase(item.name, query) &&
            !util::ContainsNoCase(item.uri, query) && !util::ContainsNoCase(item.dir, query)) {
            continue;
        }
        result.push_back(&item);
    }

    switch (sortMode_) {
    case SortMode::Name:
        std::stable_sort(result.begin(), result.end(), [](const Aria2Item* a, const Aria2Item* b) {
            return util::Lower(a->name) < util::Lower(b->name);
        });
        break;
    case SortMode::Progress:
        std::stable_sort(result.begin(), result.end(), [](const Aria2Item* a, const Aria2Item* b) {
            return a->progress() > b->progress();
        });
        break;
    case SortMode::Size:
        std::stable_sort(result.begin(), result.end(), [](const Aria2Item* a, const Aria2Item* b) {
            return a->total > b->total;
        });
        break;
    case SortMode::Status:
        std::stable_sort(result.begin(), result.end(), [](const Aria2Item* a, const Aria2Item* b) {
            return StatusRank(*a) < StatusRank(*b);
        });
        break;
    default:
        break;
    }
    return result;
}

const Aria2Item* App::findItem(const std::wstring& gid) const {
    for (const Aria2Item& item : uiSnapshot_.items)
        if (item.gid == gid) return &item;
    return nullptr;
}

int App::countForPage(Page page) const {
    if (page == Page::Settings) return -1;
    int count = 0;
    for (const Aria2Item& item : uiSnapshot_.items) {
        if (item.isMetadataPlaceholder()) continue;
        switch (page) {
        case Page::All: ++count; break;
        case Page::Active: if (item.isActive()) ++count; break;
        case Page::Waiting: if (item.isWaiting()) ++count; break;
        case Page::Paused: if (item.isPaused()) ++count; break;
        case Page::Completed: if (item.isFinished()) ++count; break;
        default: break;
        }
    }
    return count;
}

std::wstring App::pageTitle() const {
    switch (page_) {
    case Page::All: return L"All downloads";
    case Page::Active: return L"Active";
    case Page::Waiting: return L"Waiting";
    case Page::Paused: return L"Paused";
    case Page::Completed: return L"Completed";
    case Page::Settings: return L"Settings";
    default: return L"Downloads";
    }
}

std::wstring App::pageSubtitle() const {
    if (page_ == Page::Settings) return L"Application and engine preferences";
    int count = countForPage(page_);
    std::wstring text = std::to_wstring(count) + (count == 1 ? L" download" : L" downloads");
    if (uiSnapshot_.active > 0) text += L" \u00B7 " + std::to_wstring(uiSnapshot_.active) + L" active";
    return text;
}

void App::render() {
    if (width_ <= 1 || height_ <= 1) return;
    hits_.clear();
    // Keep the title bar fully transparent (Mica shows through it, like a
    // WinUI seamless title bar) and lay a single content layer below it.
    canvas_.clear(colors_.windowBg);
    if (colors_.contentBg >> 24) {
        canvas_.fillRect(Rect{0, (float)kTitleHeight, width_, height_ - kTitleHeight},
                         colors_.contentBg);
    }

    renderTitleBar();
    renderSidebar();
    if (page_ == Page::Settings)
        renderSettings();
    else
        renderDownloads();
    renderStatusBar();

    if (dialogOpen_) {
        canvas_.fillRect(Rect{0, 0, width_, height_}, colors_.overlay);
        hits_.clear();
        renderDialog();
    }

    if (megaPromptOpen_) {
        canvas_.fillRect(Rect{0, 0, width_, height_}, colors_.overlay);
        hits_.clear();
        renderMegaPrompt();
    }

    if (!toast_.empty()) {
        if (util::NowMs() > toastUntil_) {
            toast_.clear();
        } else {
            renderToast();
        }
    }
}

void App::renderTitleBar() {
    float W = width_;
    canvas_.line(0, (float)kTitleHeight - 0.5f, W, (float)kTitleHeight - 0.5f, colors_.stroke);

    // NavigationView pane toggle, sitting in the title bar like WinUI apps.
    Rect toggle{6, 8, 40, 32};
    bool toggleHover = isHovered(UI_PaneToggle);
    if (toggleHover) canvas_.fillRound(toggle, 4, colors_.captionHover);
    canvas_.glyph(toggle, 0xE700, 15, toggleHover ? colors_.text : colors_.textDim);
    addHit(UI_PaneToggle, toggle, {}, -1, IDC_HAND);

    Rect logo{52, 11, 26, 26};
    canvas_.fillRound(logo, 8, colors_.accent);
    canvas_.glyph(logo, 0xE896, 14, colors_.onAccent);
    canvas_.text(Rect{88, 14, 240, 22}, L"Aria2DM", 14.5f, colors_.text,
                 DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (page_ != Page::Settings) {
        float searchW = 360.0f;
        searchRect_ = Rect{(W - searchW) * 0.5f, 8, searchW, 32};
        drawTextField(UI_SearchField, Focus::Search, searchRect_, searchInput_,
                      L"Search downloads", 0xE721);
    }

    float by = 0.0f;
    float bw = 46.0f, bh = 32.0f;
    captionCloseRect_ = Rect{W - bw, by, bw, bh};
    captionMaxRect_ = Rect{captionCloseRect_.x - bw, by, bw, bh};
    captionMinRect_ = Rect{captionMaxRect_.x - bw, by, bw, bh};

    struct Caption {
        int id;
        Rect rect;
        wchar_t glyph;
    } captions[] = {
        {UI_CaptionMin, captionMinRect_, 0xE921},
        {UI_CaptionMax, captionMaxRect_, (wchar_t)(IsZoomed(hwnd_) ? 0xE923 : 0xE922)},
        {UI_CaptionClose, captionCloseRect_, 0xE8BB},
    };

    for (const Caption& caption : captions) {
        bool hover = isHovered(caption.id);
        bool pressed = isPressed(caption.id);
        bool close = caption.id == UI_CaptionClose;
        // The system caption buttons fill their whole 46x32 cell, with no
        // rounded corners, and the close button turns red on hover.
        if (hover || pressed) canvas_.fillRect(caption.rect, close ? colors_.closeHover : colors_.captionHover);
        Color fg = close ? (hover || pressed ? colors_.closeHoverText : colors_.textDim)
                         : (hover || pressed ? colors_.text : colors_.textDim);
        canvas_.glyph(caption.rect, caption.glyph, 10, fg);
        addHit(caption.id, caption.rect, {}, -1, IDC_ARROW);
    }
}

void App::renderSidebar() {
    float W = sidebarWidth_;
    float H = height_;
    canvas_.fillRect(Rect{0, (float)kTitleHeight, W, H - kTitleHeight}, colors_.sidebarBg);
    canvas_.line(W - 0.5f, (float)kTitleHeight, W - 0.5f, H, colors_.stroke);

    float y = kTitleHeight + 16.0f;
    drawNavItem(Page::All, L"All downloads", 0xE8B7, countForPage(Page::All), y);
    y += 44.0f;
    drawNavItem(Page::Active, L"Active", 0xE896, countForPage(Page::Active), y);
    y += 44.0f;
    drawNavItem(Page::Waiting, L"Waiting", 0xE823, countForPage(Page::Waiting), y);
    y += 44.0f;
    drawNavItem(Page::Paused, L"Paused", 0xE769, countForPage(Page::Paused), y);
    y += 44.0f;
    drawNavItem(Page::Completed, L"Completed", 0xE73E, countForPage(Page::Completed), y);

    drawNavItem(Page::Settings, L"Settings", 0xE713, -1, H - 56.0f);
}

void App::drawNavItem(Page page, const std::wstring& label, wchar_t glyph, int count, float y) {
    const float W = sidebarWidth_;
    const bool compact = W < 150.0f;
    Rect rect = compact ? Rect{4, y, W - 8, 40} : Rect{10, y, W - 20, 40};
    bool selected = page_ == page;
    bool hover = isHovered(UI_NavAll + (int)page);

    // NavigationView keeps the selected row neutral and marks it with a short
    // accent pill on the leading edge; the label itself stays primary text.
    if (selected) {
        canvas_.fillRound(rect, 4, colors_.navSelected);
        canvas_.fillRound(Rect{rect.x, rect.y + 12, 3, 16}, 1.5f, colors_.accent);
    } else if (hover) {
        canvas_.fillRound(rect, 4, colors_.captionHover);
    }

    Color fg = selected ? colors_.text : colors_.textDim;
    if (compact) {
        // Compact mode: icon rail only, exactly like a collapsed NavigationView.
        canvas_.glyph(rect, glyph, 16, fg, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        Rect iconRect{rect.x + 14, rect.y, 22, rect.h};
        canvas_.glyph(iconRect, glyph, 16, fg, DWRITE_TEXT_ALIGNMENT_CENTER);
        canvas_.text(Rect{rect.x + 44, rect.y, rect.w - 44 - (count > 0 ? 36 : 12), rect.h}, label,
                     14, fg, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (count > 0) {
            canvas_.text(Rect{rect.right() - 40, rect.y, 28, rect.h}, std::to_wstring(count), 11.5f,
                         colors_.textDim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING,
                         DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    addHit(UI_NavAll + (int)page, rect, {}, -1, IDC_HAND);
}

void App::computeTableLayout(int actionCount) {
    RowLayout L;
    const float wSize = 68.0f;
    const float wProgress = 50.0f;
    const float wSpeed = 80.0f;
    const float wStatus = 84.0f;
    const float wAdded = 94.0f;
    const float wDone = 94.0f;
    const float gap = 10.0f;
    const float iconW = 24.0f;
    const float iconGap = 4.0f;

    const float actionsW = actionCount * iconW + (actionCount - 1) * iconGap;
    L.actionsX = listRect_.right() - 10.0f - actionsW;

    // Optional columns fall away as the window narrows, so the name column
    // keeps a readable width instead of collapsing to a sliver.
    const float w = listRect_.w;
    L.showSize = w >= 380.0f;
    L.showProgress = w >= 460.0f;
    L.showSpeed = w >= 540.0f;
    L.showStatus = w >= 620.0f;
    L.showAdded = w >= 780.0f;
    L.showDone = w >= 860.0f;

    float fixed = 0.0f;
    if (L.showSize) fixed += wSize + gap;
    if (L.showProgress) fixed += wProgress + gap;
    if (L.showSpeed) fixed += wSpeed + gap;
    if (L.showStatus) fixed += wStatus + gap;
    if (L.showAdded) fixed += wAdded + gap;
    if (L.showDone) fixed += wDone + gap;

    L.nameX = listRect_.x + 44.0f;
    L.nameW = std::max(130.0f, L.actionsX - L.nameX - 12.0f - fixed);

    float cx = L.nameX + L.nameW + gap;
    if (L.showSize) {
        L.sizeX = cx;
        L.sizeW = wSize;
        cx += wSize + gap;
    }
    if (L.showProgress) {
        L.progressX = cx;
        L.progressW = wProgress;
        cx += wProgress + gap;
    }
    if (L.showSpeed) {
        L.speedX = cx;
        L.speedW = wSpeed;
        cx += wSpeed + gap;
    }
    if (L.showStatus) {
        L.statusX = cx;
        L.statusW = wStatus;
        cx += wStatus + gap;
    }
    if (L.showAdded) {
        L.addedX = cx;
        L.addedW = wAdded;
        cx += wAdded + gap;
    }
    if (L.showDone) {
        L.doneX = cx;
        L.doneW = wDone;
        cx += wDone + gap;
    }
    tableLayout_ = L;
}

void App::drawTableHeader() {
    const RowLayout& L = tableLayout_;
    const float headerH = 26.0f;
    Rect header{listRect_.x, listRect_.y - headerH, listRect_.w, headerH};

    auto label = [&](float x, float w, const std::wstring& text, DWRITE_TEXT_ALIGNMENT align) {
        canvas_.text(Rect{x, header.y, w, headerH}, text, 10.5f, colors_.textFaint,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD, align, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    label(L.nameX, L.nameW, L"NAME", DWRITE_TEXT_ALIGNMENT_LEADING);
    if (L.showSize) label(L.sizeX, L.sizeW, L"SIZE", DWRITE_TEXT_ALIGNMENT_CENTER);
    if (L.showProgress)
        label(L.progressX, L.progressW, L"PROGRESS", DWRITE_TEXT_ALIGNMENT_CENTER);
    if (L.showSpeed) label(L.speedX, L.speedW, L"SPEED", DWRITE_TEXT_ALIGNMENT_CENTER);
    if (L.showStatus) label(L.statusX, L.statusW, L"STATUS", DWRITE_TEXT_ALIGNMENT_CENTER);
    if (L.showAdded) label(L.addedX, L.addedW, L"ADDED", DWRITE_TEXT_ALIGNMENT_CENTER);
    if (L.showDone) label(L.doneX, L.doneW, L"COMPLETED", DWRITE_TEXT_ALIGNMENT_CENTER);

    canvas_.line(header.x, header.bottom() - 0.5f, header.right(), header.bottom() - 0.5f,
                 colors_.stroke);
}

void App::renderDownloads() {
    float left = sidebarWidth_;

    float headerTop = (float)kTitleHeight;
    float pad = 28.0f;
    float contentW = width_ - left;
    contentRect_ = Rect{left, headerTop, contentW, height_ - headerTop - kStatusHeight};

    canvas_.text(Rect{left + pad, headerTop + 14, contentW - pad * 2 - 200, 36}, pageTitle(), 28,
                 colors_.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    canvas_.text(Rect{left + pad, headerTop + 52, contentW - pad * 2 - 200, 18}, pageSubtitle(), 12.5f,
                 colors_.textDim);

    Rect newButton{width_ - pad - 152, headerTop + 22, 152, 32};
    float headerLeft = newButton.x;
    if (page_ == Page::Completed && countForPage(Page::Completed) > 0) {
        Rect clearButton{headerLeft - 12 - 148, newButton.y, 148, 32};
        drawGhostButton(UI_ClearCompleted, clearButton, L"Clear completed");
        headerLeft = clearButton.x;
    }
    sortButtonRect_ = Rect{headerLeft - 12 - 156, newButton.y, 156, 32};
    drawSortButton();
    drawPrimaryButton(UI_NewDownload, newButton, L"New download", true);

    float listTop = headerTop + 106.0f;
    listRect_ = Rect{left + pad, listTop, contentW - pad * 2,
                     std::max(40.0f, height_ - kStatusHeight - 6 - listTop)};
    computeTableLayout(4);

    std::vector<const Aria2Item*> items = filteredItems();
    const float rowHeight = 48.0f;
    const float gap = 2.0f;
    contentHeight_ = items.empty() ? 0.0f : (float)items.size() * (rowHeight + gap) - gap;
    float maxScroll = std::max(0.0f, contentHeight_ - listRect_.h);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);

    if (items.empty()) {
        drawCenteredEmptyState(listRect_);
    } else {
        drawTableHeader();
        canvas_.pushClip(listRect_);
        float y = listRect_.y - scroll_;
        for (const Aria2Item* item : items) {
            Rect row{listRect_.x, y, listRect_.w, rowHeight};
            if (row.bottom() > listRect_.y && row.y < listRect_.bottom()) drawCard(*item, row);
            y += rowHeight + gap;
        }
        canvas_.popClip();
    }

    if (contentHeight_ > listRect_.h) {
        float trackH = listRect_.h;
        float thumbH = std::max(32.0f, trackH * trackH / contentHeight_);
        float travel = trackH - thumbH;
        float t = maxScroll > 0 ? scroll_ / maxScroll : 0.0f;
        Rect thumb{width_ - pad + 8, listRect_.y + travel * t, 4, thumbH};
        canvas_.fillRound(thumb, 2, colors_.scrollThumb);
    }

    if (sortMenuOpen_) renderSortMenu();
}

std::wstring App::sortLabel() const {
    switch (sortMode_) {
    case SortMode::Name: return L"Name";
    case SortMode::Progress: return L"Progress";
    case SortMode::Size: return L"Size";
    case SortMode::Status: return L"Status";
    default: return L"Newest first";
    }
}

void App::drawSortButton() {
    bool hover = isHovered(UI_SortButton) || sortMenuOpen_;
    canvas_.fillRound(sortButtonRect_, 4, hover ? colors_.cardHover : colors_.fieldBg);
    canvas_.strokeRound(sortButtonRect_.inset(0.5f), 4,
                        hover ? colors_.strokeStrong : colors_.fieldStroke, 1);
    canvas_.glyph(Rect{sortButtonRect_.x + 10, sortButtonRect_.y, 22, sortButtonRect_.h}, 0xE8CB, 12,
                  colors_.textDim);
    canvas_.text(Rect{sortButtonRect_.x + 34, sortButtonRect_.y, sortButtonRect_.w - 56,
                      sortButtonRect_.h},
                 L"Sort: " + sortLabel(), 12, colors_.text, DWRITE_FONT_WEIGHT_NORMAL,
                 DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    canvas_.glyph(Rect{sortButtonRect_.right() - 24, sortButtonRect_.y, 18, sortButtonRect_.h},
                  0xE70D, 10, colors_.textFaint);
    addHit(UI_SortButton, sortButtonRect_, {}, -1, IDC_HAND);
}

void App::renderSortMenu() {
    static const wchar_t* labels[] = {L"Newest first", L"Name", L"Progress", L"Size", L"Status"};
    const int count = (int)SortMode::Count;
    const float rowH = 34.0f;
    const float menuW = 186.0f;
    const float menuH = rowH * count + 10.0f;
    Rect menu{sortButtonRect_.x + sortButtonRect_.w - menuW,
              sortButtonRect_.bottom() + 6.0f, menuW, menuH};

    canvas_.fillRound(menu.offset(0, 4).inset(-6), 12, Tint(0x000000, 0x33));
    Color panel = dark_ ? 0xFF2B2B2Bu : 0xFFFFFFFFu;
    canvas_.fillRound(menu, 8, panel);
    canvas_.strokeRound(menu.inset(0.5f), 8, colors_.strokeStrong, 1);

    for (int i = 0; i < count; ++i) {
        Rect row{menu.x + 4, menu.y + 5 + rowH * i, menu.w - 8, rowH};
        bool selected = (int)sortMode_ == i;
        bool hover = isHovered(UI_SortOptionBase + i);
        if (hover) canvas_.fillRound(row, 4, colors_.captionHover);
        canvas_.text(Rect{row.x + 12, row.y, row.w - 52, row.h}, labels[i], 12.5f,
                     selected ? colors_.text : colors_.textDim, DWRITE_FONT_WEIGHT_NORMAL,
                     DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (selected) {
            canvas_.glyph(Rect{row.right() - 30, row.y, 20, row.h}, 0xE73E, 13, colors_.accent);
        }
        addHit(UI_SortOptionBase + i, row, {}, i, IDC_HAND);
    }
}

void App::setSortMode(SortMode mode) {
    sortMode_ = mode;
    sortMenuOpen_ = false;
    scroll_ = 0.0f;
    invalidate();
}

void App::closeSortMenu() {
    if (!sortMenuOpen_) return;
    sortMenuOpen_ = false;
    invalidate();
}

void App::drawProgressFill(const Rect& rect, const Aria2Item& item) {
    double progress = item.progress();
    if (item.isComplete()) progress = 1.0;
    if (item.isRemoved()) progress = 0.0;
    const float fillRight = rect.x + rect.w * (float)progress;

    Color fill = colors_.accent;
    uint32_t fillAlpha = 0x4A;
    if (item.isComplete()) {
        fill = colors_.success;
        fillAlpha = 0x40;
    } else if (item.isError()) {
        fill = colors_.danger;
        fillAlpha = 0x36;
    } else if (item.isPaused()) {
        fill = colors_.textDim;
        fillAlpha = 0x2E;
    } else if (item.isWaiting()) {
        fill = colors_.textDim;
        fillAlpha = 0x22;
    }

    // The chart covers the whole panel, so it reads as the row background
    // rather than a small widget parked on the left.
    const Rect band{rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f};
    if (band.w <= 0.0f || band.h <= 0.0f) return;

    const SpeedHistory* hist = nullptr;
    auto histIt = speedHistory_.find(item.gid);
    if (histIt != speedHistory_.end() && !histIt->second.samples.empty()) hist = &histIt->second;
    const int count = hist ? (int)hist->samples.size() : 0;
    const float peak = hist ? std::max(hist->peak, 65536.0f) : 1.0f;

    // The newest speed sample sits at the right edge. Between samples the whole
    // chart slides left by one bar slot, so the motion is continuous instead of
    // jumping once per snapshot.
    const float sampleMs = 200.0f;
    const float barW = 3.0f;
    const float period = 4.0f; // 3 px bar + 1 px gap
    float elapsed = 0.0f;
    if (item.isActive() && hist) {
        elapsed = (float)(util::NowMs() - hist->lastSample);
        elapsed = std::clamp(elapsed, 0.0f, sampleMs);
    }
    const float offset = elapsed / sampleMs * period;

    canvas_.pushClip(rect);
    const float baseY = band.bottom();
    const int slots = (int)std::ceil(band.w / period);
    const float corner = 14.0f;

    for (int i = 0; i < slots; ++i) {
        const float xRight = band.right() - (float)i * period - offset;
        const float bx = xRight - barW;
        if (xRight <= band.x) break;
        if (bx >= band.right()) continue;

        float level = 0.0f;
        if (count > 0) {
            const int idx = std::min(i, count - 1);
            const float sample = hist->samples[count - 1 - idx];
            level = std::clamp(sample / peak, 0.0f, 1.0f);
        } else if (item.isComplete()) {
            // Restored downloads have no recorded history; show a calm,
            // uniform bar chart so the finished row still reads as complete.
            level = 0.45f;
        }
        if (level < 0.06f) level = 0.06f;

        const bool done = bx < fillRight;
        if (!done && item.isComplete()) continue;

        // Corner fades: bars dissolve near the rounded left/right ends so the
        // chart never fights the panel outline.
        const float leftFade = std::clamp((bx + barW - band.x) / corner, 0.0f, 1.0f);
        const float rightFade = std::clamp((band.right() - bx) / corner, 0.0f, 1.0f);
        float fade = leftFade * rightFade;

        // Fade the fill near the progress edge so downloaded and pending parts
        // melt into each other instead of meeting on a hard vertical line.
        if (done) {
            const float distance = fillRight - bx;
            if (distance < 36.0f) fade *= std::clamp(distance / 36.0f, 0.15f, 1.0f);
        }

        const float height = band.h * (0.45f + 0.52f * level);
        const uint32_t baseAlpha = done ? fillAlpha : (item.isActive() ? 0x0A : 0x08);
        const Color color = done ? fill : colors_.text;

        // Four stacked slices approximate a vertical fade-out gradient that
        // runs the full height of the row.
        const int slices = 4;
        for (int s = 0; s < slices; ++s) {
            const float sliceH = height / (float)slices;
            const float vfade = 1.0f - 0.55f * ((float)s / (float)(slices - 1));
            uint32_t alpha = (uint32_t)((float)baseAlpha * vfade * fade + 0.5f);
            // Quantize so the brush cache stays bounded while the bars animate.
            alpha = (alpha / 6u) * 6u;
            if (alpha < 6u) continue;
            canvas_.fillRect(Rect{bx, baseY - sliceH * (float)(s + 1), barW, sliceH + 0.5f},
                             Tint(color, alpha));
        }
    }
    canvas_.popClip();
}

void App::drawCard(const Aria2Item& item, const Rect& rect) {
    const RowLayout& L = tableLayout_;
    bool hover = isHovered(UI_Card, item.gid);
    canvas_.fillRound(rect, 5, hover ? colors_.cardHover : colors_.cardBg);
    drawProgressFill(rect, item);
    canvas_.strokeRound(rect.inset(0.5f), 5,
                        hover ? colors_.strokeStrong : colors_.stroke, 1.0f);
    addHit(UI_Card, rect, item.gid);

    Color statusColor = StatusColor(colors_, item);

    // File-type badge replaces the plain status dot: the exact extension is
    // visible at a glance while the status colour still lives in its column.
    DrawTypeBadge(canvas_, Rect{rect.x + 10, rect.y + (rect.h - 26.0f) * 0.5f, 26, 26},
                  item.name, item.isTorrent);

    std::wstring subtitle;
    Color subtitleColor = colors_.textDim;
    if (item.isError()) {
        subtitle = FriendlyError(item.errorMessage);
        subtitleColor = colors_.danger;
    } else if (item.isTorrent) {
        subtitle = L"BitTorrent";
        if (item.connections > 0)
            subtitle += L" \u00B7 " + std::to_wstring(item.connections) + L" peers";
        if (item.numSeeders > 0)
            subtitle += L" \u00B7 " + std::to_wstring(item.numSeeders) + L" seeders";
        if (item.total <= 0) subtitle = L"Fetching torrent metadata\u2026";
    } else if (!item.uri.empty()) {
        subtitle = item.uri;
    } else {
        subtitle = item.dir;
    }

    canvas_.text(Rect{L.nameX, rect.y + 4, L.nameW, 19}, item.name, 12.5f, colors_.text,
                 DWRITE_FONT_WEIGHT_SEMI_BOLD);
    canvas_.text(Rect{L.nameX, rect.y + 23, L.nameW, 15}, subtitle, 10.5f, subtitleColor);

    auto cell = [&](float x, float w, const std::wstring& text, Color color,
                    DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) {
        canvas_.text(Rect{x, rect.y, w, rect.h}, text, 11.5f, color, weight, align,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    if (L.showSize)
        cell(L.sizeX, L.sizeW, item.total > 0 ? util::FormatBytes(item.total) : L"\u2014",
             colors_.textDim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);

    if (L.showProgress && !item.isRemoved())
        cell(L.progressX, L.progressW, PercentText(item), statusColor,
             DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);

    if (L.showSpeed) {
        std::wstring speed = item.downSpeed > 0 ? util::FormatSpeed(item.downSpeed) : L"\u2014";
        cell(L.speedX, L.speedW, speed, item.downSpeed > 0 ? colors_.text : colors_.textFaint,
             DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    if (L.showStatus)
        cell(L.statusX, L.statusW, StatusText(item), statusColor, DWRITE_FONT_WEIGHT_NORMAL,
             DWRITE_TEXT_ALIGNMENT_CENTER);

    std::time_t added = 0, completed = 0;
    auto timesIt = itemTimes_.find(item.gid);
    if (timesIt != itemTimes_.end()) {
        added = timesIt->second.added;
        completed = timesIt->second.completed;
    }
    if (L.showAdded)
        cell(L.addedX, L.addedW, util::FormatTimeShort(added), colors_.textDim,
             DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (L.showDone)
        cell(L.doneX, L.doneW, util::FormatTimeShort(completed), colors_.textDim,
             DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);

    // Actions: four fixed slots so the icons line up down the whole table.
    const float iconY = rect.y + (rect.h - 24.0f) * 0.5f;
    float bx = L.actionsX;
    bool finished = item.isFinished();
    if (!finished) {
        int id = item.isPaused() ? UI_ActionResume : UI_ActionPause;
        wchar_t glyph = item.isPaused() ? 0xE768 : 0xE769;
        drawIconButton(id, Rect{bx, iconY, 24, 24}, glyph, colors_.textDim, colors_.captionHover,
                       item.gid);
    } else if (item.isError() && !item.uri.empty()) {
        drawIconButton(UI_ActionRetry, Rect{bx, iconY, 24, 24}, 0xE72C, colors_.textDim,
                       colors_.captionHover, item.gid);
    }
    bx += 28.0f;
    if (!item.uri.empty() || !item.path.empty()) {
        drawIconButton(UI_ActionCopyLink, Rect{bx, iconY, 24, 24}, 0xE71B, colors_.textDim,
                       colors_.captionHover, item.gid);
    }
    bx += 28.0f;
    drawIconButton(UI_ActionFolder, Rect{bx, iconY, 24, 24}, 0xE8B7, colors_.textDim,
                   colors_.captionHover, item.gid);
    bx += 28.0f;
    drawIconButton(UI_ActionRemove, Rect{bx, iconY, 24, 24}, 0xE74D, colors_.textDim,
                   colors_.dangerSoft, item.gid);
}

void App::drawCenteredEmptyState(const Rect& area) {
    float centerY = area.y + area.h * 0.5f;
    EngineState state = uiSnapshot_.state;

    if (state == EngineState::Starting && uiSnapshot_.items.empty()) {
        canvas_.glyph(Rect{area.x, centerY - 86, area.w, 34}, 0xE895, 28, colors_.warning);
        canvas_.text(Rect{area.x, centerY - 44, area.w, 24}, L"Starting the aria2 engine\u2026", 16,
                     colors_.text, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        canvas_.text(Rect{area.x, centerY - 16, area.w, 18}, L"This only takes a moment.", 12,
                     colors_.textDim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return;
    }

    if (state == EngineState::Error && uiSnapshot_.items.empty()) {
        canvas_.glyph(Rect{area.x, centerY - 100, area.w, 34}, 0xE783, 30, colors_.danger);
        canvas_.text(Rect{area.x, centerY - 56, area.w, 24}, L"The aria2 engine is offline", 16,
                     colors_.text, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        std::wstring message = uiSnapshot_.message.empty()
                                   ? L"Check aria2.log in the app data folder and try again."
                                   : uiSnapshot_.message;
        canvas_.text(Rect{area.x + 40, centerY - 26, area.w - 80, 18}, message, 12, colors_.textDim,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        Rect retry{area.x + (area.w - 120) * 0.5f, centerY + 10, 120, 32};
        drawPrimaryButton(UI_RetryEngine, retry, L"Retry", true);
        return;
    }

    bool searching = !searchInput_.text.empty();
    wchar_t glyph = 0xE896;
    std::wstring title = L"No downloads yet";
    std::wstring subtitle = L"Add a link or magnet link to get started.";
    if (searching) {
        glyph = 0xE721;
        title = L"No matching downloads";
        subtitle = L"Try a different search term.";
    } else if (page_ == Page::Active) {
        title = L"No active downloads";
        subtitle = L"Queued downloads start automatically.";
    } else if (page_ == Page::Waiting) {
        glyph = 0xE823;
        title = L"Nothing waiting";
        subtitle = L"Downloads appear here when the limit is reached.";
    } else if (page_ == Page::Paused) {
        glyph = 0xE769;
        title = L"No paused downloads";
        subtitle = L"Paused downloads can be resumed at any time.";
    } else if (page_ == Page::Completed) {
        glyph = 0xE73E;
        title = L"No completed downloads";
        subtitle = L"Finished downloads are collected here.";
    }

    canvas_.glyph(Rect{area.x, centerY - 96, area.w, 34}, glyph, 30, colors_.textFaint);
    canvas_.text(Rect{area.x, centerY - 52, area.w, 24}, title, 16, colors_.text,
                 DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    canvas_.text(Rect{area.x + 20, centerY - 22, area.w - 40, 18}, subtitle, 12, colors_.textDim,
                 DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    Rect button{area.x + (area.w - 152) * 0.5f, centerY + 14, 152, 32};
    drawPrimaryButton(UI_NewDownload, button, L"New download", true);
}

void App::renderSettings() {
    float left = sidebarWidth_;
    float pad = 28.0f;
    float headerTop = (float)kTitleHeight;
    float x = left + pad;
    float w = width_ - left - pad * 2;

    canvas_.text(Rect{x, headerTop + 22, w, 38}, L"Settings", 28, colors_.text,
                 DWRITE_FONT_WEIGHT_SEMI_BOLD);
    canvas_.text(Rect{x, headerTop + 62, w, 18}, pageSubtitle(), 12, colors_.textDim);

    const float downloadsH = 196.0f;
    const float engineH = 112.0f;
    const float systemH = 108.0f;
    const float browserH = 124.0f;
    const float aboutH = 70.0f;
    const float gap = 12.0f;

    float viewTop = headerTop + 92.0f;
    settingsRect_ =
        Rect{left, viewTop, width_ - left, std::max(0.0f, height_ - kStatusHeight - viewTop)};
    settingsContentHeight_ =
        downloadsH + engineH + systemH + browserH + aboutH + gap * 4.0f + 16.0f;

    float maxScroll = std::max(0.0f, settingsContentHeight_ - settingsRect_.h);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);

    auto onScreen = [this](const Rect& rect) {
        return rect.y < settingsRect_.bottom() && rect.bottom() > settingsRect_.y;
    };

    canvas_.pushClip(settingsRect_);

    float y = headerTop + 100.0f - scroll_;

    // Downloads section
    Rect downloadsCard{x, y, w, downloadsH};
    if (onScreen(downloadsCard)) {
        canvas_.fillRound(downloadsCard, 8, colors_.cardBg);
        canvas_.strokeRound(downloadsCard.inset(0.5f), 8, colors_.stroke, 1);
        canvas_.text(Rect{x + 22, y + 16, w - 44, 22}, L"Downloads", 15, colors_.text,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD);

        float row1 = y + 48;
        canvas_.text(Rect{x + 22, row1, 180, 32}, L"Save files to", 13, colors_.text,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Rect pathRect{x + 210, row1, w - 210 - 22 - 108, 32};
        canvas_.text(pathRect, settings_.downloadDir, 12, colors_.textDim, DWRITE_FONT_WEIGHT_NORMAL,
                     DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Rect browse{x + w - 22 - 96, row1, 96, 32};
        if (onScreen(browse)) drawGhostButton(UI_BrowseDir, browse, L"Browse");

        canvas_.line(x + 22, y + 88.5f, x + w - 22, y + 88.5f, colors_.stroke);

        float row2 = y + 96;
        canvas_.text(Rect{x + 22, row2, w - 22 - 150 - 22, 32}, L"Maximum concurrent downloads", 13,
                     colors_.text, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Rect maxStepper{x + w - 22 - 150, row2, 150, 32};
        if (onScreen(maxStepper)) {
            drawStepper(maxStepper, std::to_wstring(settings_.maxConcurrent), UI_MaxMinus, UI_MaxPlus,
                        settings_.maxConcurrent > 1, settings_.maxConcurrent < 16);
        }

        canvas_.line(x + 22, y + 136.5f, x + w - 22, y + 136.5f, colors_.stroke);

        float row3 = y + 144;
        canvas_.text(Rect{x + 22, row3, 220, 32}, L"Global download limit", 13, colors_.text,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        canvas_.text(Rect{x + 200, row3, 160, 32}, L"0 = unlimited", 11.5f, colors_.textFaint,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        std::wstring limitText = settings_.speedLimit > 0
                                     ? util::FormatBytes(settings_.speedLimit) + L"/s"
                                     : L"Unlimited";
        Rect limitStepper{x + w - 22 - 150, row3, 150, 32};
        if (onScreen(limitStepper)) {
            drawStepper(limitStepper, limitText, UI_LimitMinus, UI_LimitPlus,
                        settings_.speedLimit > 0, settings_.speedLimit < 1024LL * 1024 * 1024);
        }
    }
    y += downloadsH + gap;

    // Engine section
    Rect engineCard{x, y, w, engineH};
    if (onScreen(engineCard)) {
        canvas_.fillRound(engineCard, 8, colors_.cardBg);
        canvas_.strokeRound(engineCard.inset(0.5f), 8, colors_.stroke, 1);
        canvas_.text(Rect{x + 22, y + 16, w - 44, 20}, L"Engine", 15, colors_.text,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD);
        std::wstring engineLine = L"Built-in aria2 engine";
        if (!uiSnapshot_.version.empty()) engineLine += L" " + uiSnapshot_.version;
        if (uiSnapshot_.state == EngineState::Connected) {
            engineLine += L"  \u00B7  running on 127.0.0.1:" + std::to_wstring(uiSnapshot_.port);
        } else if (uiSnapshot_.state == EngineState::Starting) {
            engineLine += L"  \u00B7  starting\u2026";
        } else {
            engineLine += L"  \u00B7  offline";
        }
        canvas_.text(Rect{x + 22, y + 40, w - 44 - 160, 18}, engineLine, 12, colors_.textDim);
        canvas_.text(Rect{x + 22, y + 62, w - 44 - 160, 18},
                     L"Unpacked to " + dataDir_ + L"\\engine", 11.5f, colors_.textFaint);
        Rect openData{x + w - 22 - 150, y + 53, 150, 32};
        if (onScreen(openData)) drawGhostButton(UI_OpenData, openData, L"Open data folder");
    }
    y += engineH + gap;

    // System section
    Rect systemCard{x, y, w, systemH};
    if (onScreen(systemCard)) {
        canvas_.fillRound(systemCard, 8, colors_.cardBg);
        canvas_.strokeRound(systemCard.inset(0.5f), 8, colors_.stroke, 1);
        canvas_.text(Rect{x + 22, y + 14, w - 44, 20}, L"System", 15, colors_.text,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD);
        canvas_.text(Rect{x + 22, y + 44, w - 44 - 80, 20}, L"Start with Windows", 13, colors_.text,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        canvas_.text(Rect{x + 22, y + 66, w - 44 - 80, 18},
                     L"Open Aria2DM automatically when you sign in.", 11.5f, colors_.textFaint);
        Rect toggle{x + w - 22 - 44, y + 52, 44, 20};
        if (onScreen(toggle)) drawToggleSwitch(UI_ToggleStartup, toggle, settings_.startWithWindows);
    }
    y += systemH + gap;

    // Browser integration section
    Rect browserCard{x, y, w, browserH};
    if (onScreen(browserCard)) {
        canvas_.fillRound(browserCard, 8, colors_.cardBg);
        canvas_.strokeRound(browserCard.inset(0.5f), 8, colors_.stroke, 1);
        canvas_.text(Rect{x + 22, y + 14, w - 44, 20}, L"Browser integration", 15, colors_.text,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD);
        canvas_.text(Rect{x + 22, y + 42, w - 44, 18},
                     L"Send clicked links and browser downloads to Aria2DM.", 12, colors_.textDim);
        canvas_.text(Rect{x + 22, y + 62, w - 44, 18},
                     L"Chrome, Edge, Brave, Vivaldi, Opera and Firefox are supported.", 11.5f,
                     colors_.textFaint);
        Rect setup{x + w - 22 - 146, y + 84, 146, 32};
        if (onScreen(setup)) drawGhostButton(UI_InstallBrowser, setup, L"Set up integration");
    }
    y += browserH + gap;

    // About section
    Rect aboutCard{x, y, w, aboutH};
    if (onScreen(aboutCard)) {
        canvas_.fillRound(aboutCard, 8, colors_.cardBg);
        canvas_.strokeRound(aboutCard.inset(0.5f), 8, colors_.stroke, 1);
        canvas_.text(Rect{x + 22, y + 14, w - 44, 20}, L"Aria2DM 0.1", 13, colors_.text,
                     DWRITE_FONT_WEIGHT_SEMI_BOLD);
        canvas_.text(
            Rect{x + 22, y + 38, w - 44, 18},
            L"Single self-contained app with a built-in aria2 engine, Mica, and Fluent surfaces.",
            12, colors_.textDim);
    }

    canvas_.popClip();

    if (settingsContentHeight_ > settingsRect_.h) {
        float trackH = settingsRect_.h;
        float thumbH = std::max(32.0f, trackH * trackH / settingsContentHeight_);
        float travel = trackH - thumbH;
        float t = maxScroll > 0 ? scroll_ / maxScroll : 0.0f;
        Rect thumb{width_ - pad + 8, settingsRect_.y + travel * t, 4, thumbH};
        canvas_.fillRound(thumb, 2, colors_.scrollThumb);
    }
}

void App::renderStatusBar() {
    statusBarRect_ = Rect{sidebarWidth_, height_ - kStatusHeight,
                          width_ - sidebarWidth_, (float)kStatusHeight};
    canvas_.fillRect(statusBarRect_, colors_.contentBg);
    canvas_.line(statusBarRect_.x, statusBarRect_.y + 0.5f, width_, statusBarRect_.y + 0.5f,
                 colors_.stroke);

    Color dotColor = colors_.success;
    std::wstring label = L"aria2";
    if (uiSnapshot_.state == EngineState::Starting) {
        dotColor = colors_.warning;
        label = L"aria2 \u00B7 starting\u2026";
    } else if (uiSnapshot_.state == EngineState::Error) {
        dotColor = colors_.danger;
        label = L"aria2 \u00B7 offline";
    } else {
        label = L"aria2";
        if (!uiSnapshot_.version.empty()) label += L" " + uiSnapshot_.version;
        label += L" \u00B7 connected";
    }

    float centerY = statusBarRect_.y + statusBarRect_.h * 0.5f;
    canvas_.fillRound(Rect{statusBarRect_.x + 28, centerY - 3, 6, 6}, 3, dotColor);
    canvas_.text(Rect{statusBarRect_.x + 40, statusBarRect_.y, 260, statusBarRect_.h}, label, 11.5f,
                 colors_.textDim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (uiSnapshot_.state == EngineState::Error) {
        Rect retry{width_ - 28 - 86, statusBarRect_.y + 6, 86, 26};
        drawGhostButton(UI_RetryEngine, retry, L"Retry");
    } else {
        std::wstring right = L"\u2193 " + util::FormatSpeed(uiSnapshot_.downSpeed) + L"    \u2191 " +
                             util::FormatSpeed(uiSnapshot_.upSpeed);
        if (uiSnapshot_.active > 0 || uiSnapshot_.waiting > 0) {
            right += L"    \u00B7    " + std::to_wstring(uiSnapshot_.active) + L" active";
            if (uiSnapshot_.waiting > 0) right += L" \u00B7 " + std::to_wstring(uiSnapshot_.waiting) + L" waiting";
        }
        canvas_.text(Rect{statusBarRect_.x + 300, statusBarRect_.y, statusBarRect_.w - 328,
                          statusBarRect_.h}, right, 11.5f, colors_.textDim,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void App::renderDialog() {
    float panelW = 560.0f;
    float panelH = 376.0f;
    dialogRect_ = Rect{(width_ - panelW) * 0.5f, (height_ - panelH) * 0.5f, panelW, panelH};

    canvas_.fillRound(dialogRect_.offset(0, 10).inset(-10), 24, Tint(0x000000, 0x44));
    // A ContentDialog sits on its own opaque Fluent surface. Use the solid
    // theme base rather than the (now transparent) window layer.
    Color panelBg = dark_ ? 0xFF2C2C2Cu : 0xFFF9F9F9u;
    canvas_.fillRound(dialogRect_, 8, panelBg);
    canvas_.strokeRound(dialogRect_.inset(0.5f), 8, colors_.strokeStrong, 1);

    float x = dialogRect_.x;
    float y = dialogRect_.y;
    float w = dialogRect_.w;

    canvas_.text(Rect{x + 24, y + 20, w - 80, 26}, L"New download", 20, colors_.text,
                 DWRITE_FONT_WEIGHT_SEMI_BOLD);
    canvas_.text(Rect{x + 24, y + 48, w - 80, 18}, L"Paste a link or magnet URI to start downloading.",
                 12, colors_.textDim);

    Rect close{x + w - 24 - 32, y + 18, 32, 32};
    if (isHovered(UI_DialogClose)) canvas_.fillRound(close, 4, colors_.captionHover);
    canvas_.glyph(close, 0xE8BB, 10, colors_.textDim);
    addHit(UI_DialogClose, close, {}, -1, IDC_HAND);

    canvas_.text(Rect{x + 24, y + 80, 200, 16}, L"Link", 11.5f, colors_.textDim);
    drawTextField(UI_UrlField, Focus::Url, Rect{x + 24, y + 98, w - 48, 32}, urlInput_,
                  L"https://example.com/file.zip or magnet link", 0xE71B);

    canvas_.text(Rect{x + 24, y + 146, 260, 16}, L"File name (optional)", 11.5f, colors_.textDim);
    drawTextField(UI_NameField, Focus::Name, Rect{x + 24, y + 164, w - 48, 32}, nameInput_,
                  L"Keep the original file name", 0);

    canvas_.text(Rect{x + 24, y + 212, 200, 16}, L"Save to", 11.5f, colors_.textDim);
    Rect dirRow{x + 24, y + 230, w - 48, 32};
    canvas_.fillRound(dirRow, 4, colors_.fieldBg);
    canvas_.strokeRound(dirRow.inset(0.5f), 4, colors_.fieldStroke, 1);
    canvas_.glyph(Rect{dirRow.x + 10, dirRow.y, 24, dirRow.h}, 0xE8B7, 13, colors_.textDim);
    canvas_.text(Rect{dirRow.x + 38, dirRow.y, dirRow.w - 38 - 100, dirRow.h}, dialogDir_, 12,
                 colors_.textDim, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    Rect change{dirRow.right() - 4 - 84, dirRow.y + 4, 84, 24};
    drawGhostButton(UI_DialogBrowse, change, L"Change");

    if (!addError_.empty()) {
        canvas_.text(Rect{x + 24, y + 272, w - 48, 18}, addError_, 12, colors_.danger);
    }

    bool enabled = !util::Trim(urlInput_.text).empty();
    Rect download{x + w - 24 - 124, y + panelH - 56, 124, 32};
    Rect cancel{download.x - 12 - 100, download.y, 100, 32};
    drawGhostButton(UI_DialogCancel, cancel, L"Cancel");
    drawPrimaryButton(UI_DialogSubmit, download, L"Download", enabled);
}

void App::renderMegaPrompt() {
    float panelW = 520.0f;
    float panelH = 258.0f;
    megaDialogRect_ =
        Rect{(width_ - panelW) * 0.5f, (height_ - panelH) * 0.5f, panelW, panelH};

    canvas_.fillRound(megaDialogRect_.offset(0, 10).inset(-10), 24, Tint(0x000000, 0x44));
    Color panelBg = dark_ ? 0xFF2C2C2Cu : 0xFFF9F9F9u;
    canvas_.fillRound(megaDialogRect_, 8, panelBg);
    canvas_.strokeRound(megaDialogRect_.inset(0.5f), 8, colors_.strokeStrong, 1);

    const float x = megaDialogRect_.x;
    const float y = megaDialogRect_.y;
    const float w = megaDialogRect_.w;

    canvas_.text(Rect{x + 24, y + 20, w - 80, 26}, L"MEGA decryption key", 20, colors_.text,
                 DWRITE_FONT_WEIGHT_SEMI_BOLD);
    canvas_.text(Rect{x + 24, y + 48, w - 48, 18},
                 L"This link is password protected. Enter its key to start the download.", 12,
                 colors_.textDim);

    Rect close{x + w - 24 - 32, y + 18, 32, 32};
    if (isHovered(UI_MegaClose)) canvas_.fillRound(close, 4, colors_.captionHover);
    canvas_.glyph(close, 0xE8BB, 10, colors_.textDim);
    addHit(UI_MegaClose, close, {}, -1, IDC_HAND);

    canvas_.text(Rect{x + 24, y + 84, 240, 16}, L"Decryption key", 11.5f, colors_.textDim);
    megaFieldRect_ = Rect{x + 24, y + 102, w - 48, 32};
    drawTextField(UI_MegaKeyField, Focus::MegaKey, megaFieldRect_, megaKeyInput_,
                  L"Paste the key from the link (the part after #)", 0xE72E);

    if (!megaPromptError_.empty()) {
        canvas_.text(Rect{x + 24, y + 142, w - 48, 18}, megaPromptError_, 12, colors_.danger);
    }

    bool enabled = !util::Trim(megaKeyInput_.text).empty();
    Rect submit{x + w - 24 - 116, y + panelH - 56, 116, 32};
    Rect cancel{submit.x - 12 - 100, submit.y, 100, 32};
    drawGhostButton(UI_MegaCancel, cancel, L"Cancel");
    drawPrimaryButton(UI_MegaSubmit, submit, L"Continue", enabled);
}

void App::renderToast() {
    float size = 12.5f;
    float width = canvas_.textWidth(toast_, size) + 38.0f;
    width = std::min(width, width_ - 80.0f);
    Rect rect{(width_ - width) * 0.5f, height_ - kStatusHeight - 56.0f, width, 38};
    canvas_.fillRound(rect, 8, colors_.cardHover);
    canvas_.strokeRound(rect.inset(0.5f), 8, colors_.strokeStrong, 1);
    canvas_.text(rect, toast_, size, colors_.text, DWRITE_FONT_WEIGHT_NORMAL,
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void App::drawIconButton(int id, const Rect& rect, wchar_t glyph, Color color, Color hoverBg,
                         const std::wstring& gid) {
    bool hover = isHovered(id, gid);
    bool pressed = isPressed(id, gid);
    if (hover || pressed) canvas_.fillRound(rect, 4, hoverBg);
    canvas_.glyph(rect, glyph, 14, hover ? colors_.text : color);
    addHit(id, rect, gid, -1, IDC_HAND);
}

void App::drawTextField(int id, Focus focus, const Rect& rect, TextInput& input,
                        const std::wstring& placeholder, wchar_t icon) {
    int index = focusIndex(focus);
    if (focus != Focus::MegaKey) fieldRects_[index] = rect;

    bool focused = focus_ == focus;
    input.focused = focused;
    canvas_.fillRound(rect, 4, colors_.fieldBg);
    canvas_.strokeRound(rect.inset(0.5f), 4, focused ? colors_.accent : colors_.fieldStroke,
                        focused ? 1.0f : 1.0f);
    // WinUI TextBox shows a 2px accent underline while focused.
    if (focused)
        canvas_.fillRect(Rect{rect.x + 4, rect.bottom() - 2, rect.w - 8, 2}, colors_.accent);

    float textX = rect.x + 12.0f;
    if (icon) {
        canvas_.glyph(Rect{rect.x + 8, rect.y, 26, rect.h}, icon, 13, colors_.textFaint);
        textX += 28.0f;
    }
    float textW = rect.right() - 12.0f - textX;
    Rect textArea{textX, rect.y + 4, textW, rect.h - 8};
    if (focus == Focus::MegaKey)
        megaFieldTextRect_ = textArea;
    else
        fieldTextRects_[index] = textArea;
    if (textW <= 4) return;

    float totalWidth = canvas_.textWidth(input.text, kFieldSize);
    float caretX = input.caretX(canvas_, kFieldSize);
    if (totalWidth <= textW) {
        input.scroll = 0.0f;
    } else {
        if (caretX - input.scroll > textW - 2) input.scroll = caretX - textW + 2;
        if (caretX - input.scroll < 0) input.scroll = caretX;
        input.scroll = std::clamp(input.scroll, 0.0f, std::max(0.0f, totalWidth - textW));
    }

    float lineH = canvas_.lineHeight(kFieldSize);
    float lineY = rect.y + (rect.h - lineH) * 0.5f;

    canvas_.pushClip(textArea);
    if (input.text.empty()) {
        canvas_.text(Rect{textX, lineY, textW, lineH}, placeholder, kFieldSize, colors_.textFaint,
                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    } else {
        if (input.hasSelection()) {
            auto sel = input.selection();
            float x1 = textX + input.caretXPrefix(canvas_, kFieldSize, sel.first) - input.scroll;
            float x2 = textX + input.caretXPrefix(canvas_, kFieldSize, sel.second) - input.scroll;
            canvas_.fillRect(Rect{x1, lineY, std::max(1.0f, x2 - x1), lineH}, colors_.selection);
        }
        canvas_.text(Rect{textX - input.scroll, lineY, 4096, lineH}, input.text, kFieldSize,
                     colors_.text, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    }
    if (focused) {
        bool caretVisible = ((util::NowMs() - caretReset_) / 530) % 2 == 0;
        if (caretVisible) {
            float cx = textX + caretX - input.scroll;
            canvas_.fillRect(Rect{cx, lineY + 2, 1.5f, lineH - 4}, colors_.accent);
        }
    }
    canvas_.popClip();
    addHit(id, rect, {}, -1, IDC_IBEAM);
}

void App::drawStepper(const Rect& rect, const std::wstring& value, int minusId, int plusId,
                      bool canMinus, bool canPlus) {
    canvas_.fillRound(rect, 4, colors_.fieldBg);
    canvas_.strokeRound(rect.inset(0.5f), 4, colors_.fieldStroke, 1);

    Rect minus{rect.x + 2, rect.y + 2, 30, rect.h - 4};
    Rect plus{rect.right() - 32, rect.y + 2, 30, rect.h - 4};

    if (isHovered(minusId) && canMinus) canvas_.fillRound(minus, 7, colors_.captionHover);
    canvas_.glyph(minus, 0xE738, 11, canMinus ? colors_.textDim : colors_.textFaint);
    addHit(minusId, minus, {}, -1, IDC_HAND);

    if (isHovered(plusId) && canPlus) canvas_.fillRound(plus, 7, colors_.captionHover);
    canvas_.glyph(plus, 0xE710, 11, canPlus ? colors_.textDim : colors_.textFaint);
    addHit(plusId, plus, {}, -1, IDC_HAND);

    Rect valueRect{minus.right(), rect.y, plus.x - minus.right(), rect.h};
    canvas_.text(valueRect, value, 13, colors_.text, DWRITE_FONT_WEIGHT_NORMAL,
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void App::drawToggleSwitch(int id, const Rect& rect, bool on) {
    bool hover = isHovered(id);
    bool pressed = isPressed(id);

    Color track = on ? colors_.accent : colors_.fieldBg;
    if (hover) track = on ? colors_.accentHover : Blend(colors_.fieldBg, colors_.text, 0.08f);
    if (pressed) track = Blend(track, 0x000000u, 0.12f);

    float radius = rect.h * 0.5f;
    canvas_.fillRound(rect, radius, track);
    if (!on) canvas_.strokeRound(rect.inset(0.5f), radius, colors_.fieldStroke, 1);

    float thumbSize = rect.h - 8.0f;
    float thumbX = on ? rect.right() - thumbSize - 4.0f : rect.x + 4.0f;
    canvas_.fillRound(Rect{thumbX, rect.y + 4, thumbSize, thumbSize}, thumbSize * 0.5f,
                      on ? colors_.onAccent : colors_.textDim);

    addHit(id, rect, {}, -1, IDC_HAND);
}

void App::drawPrimaryButton(int id, const Rect& rect, const std::wstring& label, bool enabled) {
    bool hover = enabled && isHovered(id);
    Color bg = enabled ? (hover ? colors_.accentHover : colors_.accent) : Tint(colors_.textFaint, 0x33);
    if (enabled && isPressed(id)) bg = colors_.accentHover;
    canvas_.fillRound(rect, 4, bg);
    canvas_.text(rect, label, 14, enabled ? colors_.onAccent : colors_.textFaint,
                 DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (enabled) addHit(id, rect, {}, -1, IDC_HAND);
}

void App::drawGhostButton(int id, const Rect& rect, const std::wstring& label) {
    bool hover = isHovered(id);
    canvas_.fillRound(rect, 4, hover ? colors_.cardHover : colors_.fieldBg);
    canvas_.strokeRound(rect.inset(0.5f), 4, hover ? colors_.strokeStrong : colors_.fieldStroke, 1);
    canvas_.text(rect, label, 13.5f, colors_.text, DWRITE_FONT_WEIGHT_NORMAL,
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    addHit(id, rect, {}, -1, IDC_HAND);
}

void App::drawPill(const Rect& rect, const std::wstring& label, Color dotColor, Color textColor) {
    canvas_.fillRound(rect, rect.h * 0.5f, colors_.fieldBg);
    canvas_.fillRound(Rect{rect.x + 10, rect.y + rect.h * 0.5f - 3, 6, 6}, 3, dotColor);
    canvas_.text(Rect{rect.x + 22, rect.y, rect.w - 30, rect.h}, label, 11, textColor,
                 DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

bool App::isHovered(int id, const std::wstring& gid) const {
    return hoverId_ == id && hoverGid_ == gid;
}

bool App::isPressed(int id, const std::wstring& gid) const {
    return pressedId_ == id && pressedGid_ == gid;
}

void App::addHit(int id, const Rect& rect, const std::wstring& gid, int index, LPCWSTR cursor) {
    if (id == 0 || rect.empty()) return;
    Hit hit;
    hit.id = id;
    hit.gid = gid;
    hit.index = index;
    hit.rect = rect;
    hit.cursor = cursor;
    hits_.push_back(std::move(hit));
}

const Hit* App::hitTest(float x, float y) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->rect.contains(x, y)) return &*it;
    }
    return nullptr;
}




