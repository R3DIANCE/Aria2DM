#pragma once

enum : int {
    UI_None = 0,

    UI_NavAll = 20,
    UI_NavActive,
    UI_NavWaiting,
    UI_NavPaused,
    UI_NavCompleted,
    UI_NavSettings,

    UI_CaptionMin = 30,
    UI_CaptionMax,
    UI_CaptionClose,

    UI_NewDownload = 40,
    UI_ClearCompleted,
    UI_SearchField,
    UI_PaneToggle,

    UI_Card = 50,

    UI_SortButton = 56,
    UI_SortOptionBase = 130,
    UI_SortOptionLast = 135,

    UI_MegaClose = 140,
    UI_MegaCancel,
    UI_MegaSubmit,

    UI_ActionPause = 60,
    UI_ActionResume,
    UI_ActionFolder,
    UI_ActionRemove,
    UI_ActionRetry,
    UI_ActionCopyLink,

    UI_RetryEngine = 70,

    UI_BrowseDir = 80,
    UI_OpenData,
    UI_OpenDownloads,
    UI_InstallBrowser,

    UI_MaxMinus = 90,
    UI_MaxPlus,
    UI_LimitMinus,
    UI_LimitPlus,

    UI_DialogClose = 100,
    UI_DialogCancel,
    UI_DialogSubmit,
    UI_DialogBrowse,

    UI_UrlField = 110,
    UI_NameField,
    UI_MegaKeyField,

    UI_ToggleStartup = 120,
};

inline constexpr UINT WM_APP_SNAPSHOT = WM_APP + 1;
inline constexpr UINT WM_APP_TOAST = WM_APP + 2;
inline constexpr UINT WM_APP_SHOW = WM_APP + 3;
inline constexpr UINT WM_APP_TRAY = WM_APP + 4;
inline constexpr UINT WM_APP_INGEST = WM_APP + 5;
