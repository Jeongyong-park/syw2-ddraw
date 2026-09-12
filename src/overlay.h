#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>
#include <string>
#include "syw2x_palette.h"
namespace hq {
// A borderless child panel, painted in client coordinates, independent of game palettes.
struct Overlay {
    std::vector<uint32_t> background;
    int image_width=0,image_height=0;
    bool integer_scaling=false;
    bool syw2x_page=false,syw2x_available=false;
    bool notice_page=false;
    std::wstring aspect_error;
    bool aspect_available=false,aspect_wide=false,aspect_saved_wide=false;
    bool aspect_selection_changed() const { return aspect_available && aspect_wide!=aspect_saved_wide; }
    Palette palette=syw2x_palette;
    int palette_target=-1, palette_index=0;
    bool palette_command(HWND,int,int);
    void refresh_swatches(HWND);
    std::wstring syw2x_state,syw2x_status;
    HFONT heading=nullptr,body=nullptr,small_font=nullptr;
    float scale=1; int x=0,y=0;
    ~Overlay();
    void init(HWND);
    void layout(HWND);
    void paint(HWND,HDC);
    void button(HWND,const DRAWITEMSTRUCT&);
    RECT rect(int,int,int,int) const;
};
}
