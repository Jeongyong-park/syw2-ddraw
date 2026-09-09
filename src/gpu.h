#pragma once
#include <windows.h>
#include <memory>
#include "pixels.h"
#include "viewport.h"
namespace hq {
// Blt-model presentation deliberately retains HWND/GDI child-window compatibility.
class Gpu {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Gpu();
    ~Gpu();
    HRESULT present(HWND window, const Image& source, const Palette& palette,
                    Viewport viewport, bool vsync, bool linear,
                    std::vector<uint32_t>* capture=nullptr);
};
}
