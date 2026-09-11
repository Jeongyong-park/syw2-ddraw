#pragma once
#include <cstdint>
namespace hq {
struct WideRuntimeResult {
    bool available=false, wide=false;
    const char* reason="unsupported executable";
    uintptr_t allocation=0;
};
WideRuntimeResult initialize_widescreen(const wchar_t* ini);
// Core also accepts a private mapped fixture for native transaction tests.
// Production calls this only after checking the executable file's SHA-256.
WideRuntimeResult apply_widescreen_image(uint8_t* image,bool wanted,bool syw_viewport=false);
}
