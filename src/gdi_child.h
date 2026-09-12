#pragma once
#include <windows.h>
#include "viewport.h"

namespace hq {
// Draw owns registration/subclass lifetime and removes its map entry before
// restoring a copied state. Original fonts are borrowed; scaled fonts are freed
// explicitly by release(), including when the HWND has already been destroyed.
struct GdiChild {
    RECT logical{};
    HFONT original_font=nullptr,scaled_font=nullptr;
    LOGFONTW font{};
    int font_height=0,font_width=0;
    bool original_clip_siblings=false,overlay_clipping=false;

    void capture(HWND window);
    bool set_original_font(HFONT value);
    void update_clipping(HWND window,bool enabled);
    void scale_font(HWND window,const Viewport& viewport,int game_width,int game_height);
    bool release(HWND window,bool restore_window) const;
    static RECT map_rect(HWND child,HWND owner,RECT logical,Viewport viewport);
    static void position(HWND window,const RECT& target);
};
}
