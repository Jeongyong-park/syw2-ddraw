#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hq {
// Historical 512-row CPU cache model used only by widescreen_test.
// Production uses widescreen_runtime.cpp and a 608-row cache recipe.
// Keep visible width separate from the 64x32 dirty-block grid and source pitch.
struct WideTerrain {
    static constexpr int width=1068, height=600, hud_width=800;
    static constexpr int hud_x=(width-hud_width)/2;
    static constexpr int cache_width=((width+63)/64)*64;
    static constexpr int cache_height=512;
    static constexpr int columns=cache_width/64, rows=cache_height/32;
    std::vector<uint8_t> pixels=std::vector<uint8_t>(cache_width*cache_height);
    // Original engine dirty flags are column-major: column*16 + row.
    std::vector<uint8_t> dirty=std::vector<uint8_t>(columns*rows,1);

    void mark(int left,int top,int right,int bottom) {
        // Half-open cache coordinates. Clamp before converting to block indices.
        left=std::max(0,left); top=std::max(0,top);
        right=std::min(cache_width,right); bottom=std::min(cache_height,bottom);
        if(left>=right || top>=bottom) return;
        for(int col=left/64;col<=(right-1)/64;++col)
            for(int row=top/32;row<=(bottom-1)/32;++row) dirty[col*rows+row]=1;
    }

    bool copy_visible(uint8_t* dest,size_t size,int pitch,int count) const {
        // Destination is a separate surface; never use visible width as source pitch.
        if(!dest || pitch<width || count<0 || count>cache_height) return false;
        if(count && (size<size_t(width) || size_t(count-1)>(size-width)/size_t(pitch))) return false;
        for(int y=0;y<count;++y)
            std::memcpy(dest+size_t(y)*pitch,pixels.data()+size_t(y)*cache_width,width);
        return true;
    }

    void scroll(int dx,int dy) {
        // Positive delta moves existing pixels right/down; exposed regions are cleared.
        if(dx<=-cache_width || dx>=cache_width || dy<=-cache_height || dy>=cache_height) {
            std::fill(pixels.begin(),pixels.end(),uint8_t(0));
            std::fill(dirty.begin(),dirty.end(),uint8_t(1)); return;
        }
        const int left=std::max(0,dx),right=std::min(cache_width,cache_width+dx);
        const int top=std::max(0,dy),bottom=std::min(cache_height,cache_height+dy);
        const int start=dy>0?bottom-1:top,stop=dy>0?top-1:bottom,step=dy>0?-1:1;
        for(int y=start;y!=stop;y+=step) {
            auto line=pixels.data()+size_t(y)*cache_width;
            std::memmove(line+left,pixels.data()+size_t(y-dy)*cache_width+left-dx,right-left);
            std::fill(line,line+left,uint8_t(0)); std::fill(line+right,line+cache_width,uint8_t(0));
        }
        std::fill(pixels.begin(),pixels.begin()+size_t(top)*cache_width,uint8_t(0));
        std::fill(pixels.begin()+size_t(bottom)*cache_width,pixels.end(),uint8_t(0));
        // Conservative prototype: redraw all blocks after scrolling until the
        // engine's dirty-block transition rules have been independently verified.
        std::fill(dirty.begin(),dirty.end(),uint8_t(1));
    }
};
}
