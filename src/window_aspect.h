#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>

namespace hq {
// Constrain the client area, keeping the edge opposite the drag stationary.
inline bool constrain_window_aspect(RECT& rect, const RECT& current, WPARAM edge, int frame_width,
    int frame_height, int game_width, int game_height) {
    if(edge<WMSZ_LEFT || edge>WMSZ_BOTTOMRIGHT || game_width<=0 || game_height<=0) return false;
    // The experimental framebuffer includes alignment pixels beyond exact 16:9.
    if(game_width==1068 && game_height==600) { game_width=16; game_height=9; }
    const auto rounded=[](int value,int numerator,int denominator) {
        return int((int64_t(value)*numerator+denominator/2)/denominator);
    };
    const int min_width=std::max(1,160-frame_width);
    const int min_height=std::max(1,120-frame_height);
    int width=std::max(min_width,int(rect.right-rect.left)-frame_width);
    int height=std::max(min_height,int(rect.bottom-rect.top)-frame_height);
    const bool corner=edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT ||
        edge==WMSZ_BOTTOMLEFT || edge==WMSZ_BOTTOMRIGHT;
    const auto magnitude=[](int64_t value) { return value<0?-value:value; };
    const int64_t delta_width=int64_t(rect.right)-rect.left-(int64_t(current.right)-current.left);
    const int64_t delta_height=int64_t(rect.bottom)-rect.top-(int64_t(current.bottom)-current.top);
    const bool height_driven=edge==WMSZ_TOP || edge==WMSZ_BOTTOM ||
        (corner && magnitude(delta_height)*game_width>magnitude(delta_width)*game_height);
    if(height_driven) {
        height=std::max(height,int((int64_t(min_width)*game_height+game_width-1)/game_width));
        width=rounded(height,game_width,game_height);
    } else {
        width=std::max(width,int((int64_t(min_height)*game_width+game_height-1)/game_height));
        height=rounded(width,game_height,game_width);
    }
    width+=frame_width; height+=frame_height;
    if(edge==WMSZ_LEFT || edge==WMSZ_TOPLEFT || edge==WMSZ_BOTTOMLEFT) rect.left=rect.right-width;
    else rect.right=rect.left+width;
    if(edge==WMSZ_TOP || edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT) rect.top=rect.bottom-height;
    else rect.bottom=rect.top+height;
    return true;
}
}
