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
                    RECT current{-1400,-200,-1400+800+frame.x,-200+600+frame.y};
                    CHECK(hq::constrain_window_aspect(rect,current,edge,frame.x,frame.y,mode.x,mode.y));
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
    // Corner drags must accept either axis, including inward and diagonal drags.
    for(auto ratio:{POINT{4,3},POINT{16,9}}) {
        for(WPARAM edge:{WMSZ_TOPLEFT,WMSZ_TOPRIGHT,WMSZ_BOTTOMLEFT,WMSZ_BOTTOMRIGHT}) {
            for(auto delta:{POINT{0,90},POINT{0,-90},POINT{160,0},POINT{-160,0},POINT{1,90}}) {
                RECT current{100,100,100+ratio.x*100+16,100+ratio.y*100+39};
                RECT rect=current;
                const bool left=edge==WMSZ_TOPLEFT || edge==WMSZ_BOTTOMLEFT;
                const bool top=edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT;
                if(left) rect.left-=delta.x; else rect.right+=delta.x;
                if(top) rect.top-=delta.y; else rect.bottom+=delta.y;
                CHECK(hq::constrain_window_aspect(rect,current,edge,16,39,ratio.x,ratio.y));
                if(delta.y) CHECK(rect.bottom-rect.top==current.bottom-current.top+delta.y);
                else CHECK(rect.right-rect.left==current.right-current.left+delta.x);
                CHECK(left?rect.right==current.right:rect.left==current.left);
                CHECK(top?rect.bottom==current.bottom:rect.top==current.top);
            }
        }
    }
    RECT rect{0,0,500,500},before=rect;
    CHECK(!hq::constrain_window_aspect(rect,before,0,16,39,800,600));
    CHECK(EqualRect(&rect,&before));
    CHECK(!hq::constrain_window_aspect(rect,before,WMSZ_RIGHT,16,39,0,600));
    std::puts("PASS: aspect, eight drag edges, frame sizes, minimum size and negative coordinates");
}
