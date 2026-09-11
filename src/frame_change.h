#pragma once
#include "pixels.h"

namespace hq {
// Exact RGB comparison on a two-pixel grid, not a simulation/animation counter.
class FrameChange {
    std::vector<uint32_t> previous;
    int width=0,height=0,bits=0;
    Rect region{};
public:
    struct Result { int changed=0, compared=0; bool baseline=false; };
    void reset() { previous.clear(); }
    Result sample(const Image& image,const Palette& palette,Rect r) {
        if(r.left<0 || r.top<0 || r.right>image.width || r.bottom>image.height ||
           r.right<=r.left || r.bottom<=r.top ||
           (image.bpp!=8 && image.bpp!=16 && image.bpp!=32)) { reset(); return {}; }
        if(width!=image.width || height!=image.height || bits!=image.bpp ||
           region.left!=r.left || region.top!=r.top || region.right!=r.right || region.bottom!=r.bottom) reset();
        width=image.width; height=image.height; bits=image.bpp; region=r;
        Result result; result.baseline=previous.empty();
        const size_t count=size_t((r.right-r.left+1)/2)*size_t((r.bottom-r.top+1)/2);
        previous.resize(count);
        size_t i=0;
        for(int y=r.top;y<r.bottom;y+=2) for(int x=r.left;x<r.right;x+=2,++i) {
            auto value=image.read(x,y);
            if(bits==8) value=palette[value&255];
            else if(bits==16) value=(((value>>11)&31)*255/31<<16)|(((value>>5)&63)*255/63<<8)|((value&31)*255/31);
            value&=0x00ffffff;
            if(!result.baseline) { ++result.compared; if(previous[i]!=value) ++result.changed; }
            previous[i]=value;
        }
        return result;
    }
};
}
