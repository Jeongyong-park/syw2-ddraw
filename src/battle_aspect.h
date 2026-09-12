#pragma once
#include <cstdint>

namespace hq {
// Startup-only aspect selection. Preserves reversible prototype EXEs; ASI builds
// also support the verified original executable through widescreen_runtime.
struct BattleAspect {
    bool available=false, wide=false;
    const char* reason="legacy prototype or unsupported executable";
    static bool requested(const wchar_t* value);
    static BattleAspect initialize(const wchar_t* ini);
    // Prototype compatibility only. Tests supply a private mapped image.
    static BattleAspect initialize_image(uint8_t* base,const wchar_t* ini);
};
}
