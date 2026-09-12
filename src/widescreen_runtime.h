#pragma once
#include <cstdint>
namespace hq {
struct WideRuntimeResult {
    bool available=false, wide=false;
    const char* reason="unsupported executable";
    uintptr_t allocation=0;
};
// Production entry point: verifies the executable file and reads plugin settings.
WideRuntimeResult initialize_widescreen(const wchar_t* ini);
// Memory-only application (widescreen_image.cpp); does not read files or INIs.
// Accepts a private mapped fixture for native transaction tests. Production calls
// this only after checking the executable file's SHA-256. Successful allocations
// live until process exit because the patched game retains pointers into them.
WideRuntimeResult apply_widescreen_image(uint8_t* image,bool wanted,bool syw_viewport=false);
}
