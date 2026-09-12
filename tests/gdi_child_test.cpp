#include "../src/gdi_child.h"
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int main() {
    HWND owner=nullptr;
    HFONT original=nullptr;
    try {
        owner=CreateWindowW(L"STATIC",L"child test",WS_OVERLAPPED,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        CHECK(owner);
        auto edit=CreateWindowW(L"EDIT",L"text",WS_CHILD,10,20,100,30,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
        CHECK(edit);
        LOGFONTW description{}; description.lfHeight=-12;
        original=CreateFontIndirectW(&description); CHECK(original);
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(original),FALSE);
        hq::GdiChild state; state.capture(edit);
        const RECT logical=state.logical;
        CHECK(state.original_font==original);
        const hq::Viewport viewport{50,25,1600,1200,800,600};
        auto target=hq::GdiChild::map_rect(edit,owner,logical,viewport);
        CHECK(target.left==70 && target.top==65);
        hq::GdiChild::position(edit,target);
        state.scale_font(edit,viewport,800,600);
        auto scaled=state.scaled_font;
        CHECK(scaled && scaled!=original);
        LOGFONTW actual{}; CHECK(GetObjectW(scaled,sizeof(actual),&actual));
        CHECK(actual.lfHeight==-24);
        state.scale_font(edit,viewport,800,600); CHECK(state.scaled_font==scaled);
        state.update_clipping(edit,true);
        CHECK(GetWindowLongPtrW(edit,GWL_STYLE)&WS_CLIPSIBLINGS);
        CHECK(state.release(edit,true));
        CHECK(!(GetWindowLongPtrW(edit,GWL_STYLE)&WS_CLIPSIBLINGS));
        CHECK(reinterpret_cast<HFONT>(SendMessageW(edit,WM_GETFONT,0,0))==original);
        CHECK(GetObjectType(original)==OBJ_FONT);
        RECT restored{}; GetWindowRect(edit,&restored);
        MapWindowPoints(HWND_DESKTOP,owner,reinterpret_cast<POINT*>(&restored),2);
        CHECK(EqualRect(&restored,&logical));
        hq::GdiChild destroyed; destroyed.capture(edit);
        destroyed.scale_font(edit,viewport,800,600); scaled=destroyed.scaled_font;
        CHECK(DestroyWindow(edit)); CHECK(destroyed.release(edit,false));
        CHECK(GetObjectType(original)==OBJ_FONT);
        DestroyWindow(owner); owner=nullptr;
        CHECK(DeleteObject(original)); original=nullptr;
        std::puts("PASS: child position/font/style restoration and owned font lifetime");
    } catch(const std::exception& error) {
        if(owner) DestroyWindow(owner);
        if(original) DeleteObject(original);
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
