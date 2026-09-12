#include "../src/window_aspect.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    for(auto mode:{POINT{800,600},POINT{1068,600},POINT{831,624}}) {
        const int numerator=mode.x==1068?16:mode.x;
        const int denominator=mode.x==1068?9:mode.y;
        for(auto frame:{POINT{16,39},POINT{32,78}}) {
            for(auto size:{POINT{913,517},POINT{1,1},POINT{2400,1600}}) {
                for(WPARAM edge=WMSZ_LEFT;edge<=WMSZ_BOTTOMRIGHT;++edge) {
                    RECT rect{-1400,-200,-1400+size.x,-200+size.y};
                    const RECT before=rect;
                    CHECK(hq::constrain_window_aspect(rect,edge,frame.x,frame.y,mode.x,mode.y));
                    const auto width=rect.right-rect.left-frame.x;
                    const auto height=rect.bottom-rect.top-frame.y;
                    CHECK(rect.right-rect.left>=160 && rect.bottom-rect.top>=120);
                    CHECK(std::abs(int64_t(width)*denominator-int64_t(height)*numerator)<=std::max(numerator,denominator)/2);
                    if(edge==WMSZ_LEFT || edge==WMSZ_TOPLEFT || edge==WMSZ_BOTTOMLEFT) CHECK(rect.right==before.right);
                    else CHECK(rect.left==before.left);
                    if(edge==WMSZ_TOP || edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT) CHECK(rect.bottom==before.bottom);
                    else CHECK(rect.top==before.top);
                }
            }
        }
    }
    RECT rect{0,0,500,500},before=rect;
    CHECK(!hq::constrain_window_aspect(rect,0,16,39,800,600));
    CHECK(EqualRect(&rect,&before));
    CHECK(!hq::constrain_window_aspect(rect,WMSZ_RIGHT,16,39,0,600));
    std::puts("PASS: aspect, eight drag edges, frame sizes, minimum size and negative coordinates");
}
