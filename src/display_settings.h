#pragma once
#include "scaling.h"
#include <optional>
#include <string>

namespace hq {
// Preferences are separate from renderer availability and live game state.
struct DisplaySettings {
    bool windowed=true, gpu=true, vsync=false, osd=false;
    int scaling=SharpBilinear;
    std::wstring renderer_name=L"auto";
};
struct DisplaySaveResult {
    bool all_saved=true;
    bool aspect_saved=false; // false when aspect was omitted or its write failed
};
DisplaySettings load_display_settings(const wchar_t* path);
bool load_saved_battle_aspect(const wchar_t* path,bool fallback_wide);
// Writes only the display keys previously saved by the settings dialog.
// Diagnostics and unrelated keys remain untouched. Each write is attempted;
// partial INI writes are reported, not presented as an atomic transaction.
DisplaySaveResult save_display_settings(const wchar_t* path,const DisplaySettings& settings,
                                       std::optional<bool> aspect=std::nullopt);
}
