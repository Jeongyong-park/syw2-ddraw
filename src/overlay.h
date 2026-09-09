#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>
namespace hq {
// A borderless child panel, painted in client coordinates, independent of game palettes.
struct Overlay {
    std::vector<uint32_t> background;
    int image_width=0,image_height=0;
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
