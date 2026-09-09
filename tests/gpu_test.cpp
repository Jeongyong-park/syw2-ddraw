#include "../src/gpu.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"HQGpuTest";
    CHECK(RegisterClassW(&wc));
    auto window=CreateWindowW(wc.lpszClassName,L"HQ GPU pixel test",WS_POPUP,0,0,80,60,nullptr,nullptr,wc.hInstance,nullptr);
    CHECK(window);
    {
        hq::Gpu gpu; hq::Palette pal{}; pal[1]=0xff0000; pal[2]=0x00ff00;
        std::vector<uint32_t> output;
        for(int bpp:{8,16,32}) {
            // Odd width exercises row pitch, and distinct corners catch UV inversion.
            hq::Image im(7,5,bpp);
            hq::fill(im,{0,0,7,5},bpp==8?1:bpp==16?0xf800:0xff0000);
            im.write(6,4,bpp==8?2:bpp==16?0x07e0:0x00ff00);
            CHECK(SUCCEEDED(gpu.present(window,im,pal,hq::Viewport::fit(80,60,7,5),false,false,&output)));
            CHECK(output.size()==4800);
            CHECK(output[10*80+10]==0xff0000);
            CHECK(output[55*80+75]==0x00ff00);
            CHECK(output[0]==0); // letterbox
        }
        hq::Image im(2,1,8); im.write(0,0,1); im.write(1,0,2);
        pal[1]=0x0000ff;
        CHECK(SUCCEEDED(gpu.present(window,im,pal,hq::Viewport::fit(80,60,2,1),false,false,&output)));
        CHECK(output[30*80+10]==0x0000ff);
        CHECK(SUCCEEDED(gpu.present(window,im,pal,hq::Viewport::fit(80,60,2,1),false,true,&output)));
        auto middle=output[30*80+40]; CHECK((middle&255)>100 && ((middle>>8)&255)>100 && (middle>>16)==0);
        SetWindowPos(window,nullptr,0,0,120,90,SWP_NOACTIVATE|SWP_NOZORDER);
        CHECK(SUCCEEDED(gpu.present(window,im,pal,hq::Viewport::fit(120,90,2,1),false,false,&output)));
        CHECK(output.size()==10800 && output[45*120+10]==0x0000ff);
    }
    DestroyWindow(window); UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::puts("PASS: hardware GPU readback, 8/16/32-bit colors, palette update, UV, pitch, bars, linear filtering, resize");
}
