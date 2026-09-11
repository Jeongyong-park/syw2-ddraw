#include "gdi_child.h"
#include <algorithm>

namespace hq {
void GdiChild::capture(HWND window) {
    GetWindowRect(window,&logical);
    MapWindowPoints(HWND_DESKTOP,GetParent(window),reinterpret_cast<POINT*>(&logical),2);
    original_font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));
    if(!original_font) original_font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    GetObjectW(original_font,sizeof(font),&font);
}
bool GdiChild::set_original_font(HFONT value) {
    if(!value) value=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if(!GetObjectW(value,sizeof(font),&font)) return false;
    original_font=value;
    font_height=0; font_width=0;
    return true;
}
void GdiChild::update_clipping(HWND window,bool enabled) {
    if(enabled==overlay_clipping) return;
    const auto style=GetWindowLongPtrW(window,GWL_STYLE);
    if(enabled) original_clip_siblings=(style&WS_CLIPSIBLINGS)!=0;
    overlay_clipping=enabled;
    SetWindowLongPtrW(window,GWL_STYLE,(style&~LONG_PTR(WS_CLIPSIBLINGS))|
        ((enabled || original_clip_siblings)?WS_CLIPSIBLINGS:0));
}
void GdiChild::scale_font(HWND window,const Viewport& viewport,int game_width,int game_height) {
    LOGFONTW scaled=font;
    scaled.lfHeight=MulDiv(scaled.lfHeight,viewport.height,game_height);
    scaled.lfWidth=MulDiv(scaled.lfWidth,viewport.width,game_width);
    if(!scaled.lfHeight) scaled.lfHeight=-1;
    if(scaled.lfHeight!=font_height || scaled.lfWidth!=font_width) {
        HFONT created=CreateFontIndirectW(&scaled);
        if(created) {
            SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(created),TRUE);
            if(scaled_font) DeleteObject(scaled_font);
            scaled_font=created; font_height=scaled.lfHeight; font_width=scaled.lfWidth;
        }
    }
}
bool GdiChild::release(HWND window,bool restore_window) const {
    if(restore_window && IsWindow(window)) {
        if(overlay_clipping) {
            const auto style=GetWindowLongPtrW(window,GWL_STYLE)&~LONG_PTR(WS_CLIPSIBLINGS);
            SetWindowLongPtrW(window,GWL_STYLE,style|(original_clip_siblings?WS_CLIPSIBLINGS:0));
        }
        SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(original_font),TRUE);
        SetWindowPos(window,nullptr,logical.left,logical.top,logical.right-logical.left,
                     logical.bottom-logical.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    return !scaled_font || DeleteObject(scaled_font)!=FALSE;
}
RECT GdiChild::map_rect(HWND child,HWND owner,RECT logical,Viewport viewport) {
    if(GetParent(child)!=owner) { viewport.x=0; viewport.y=0; }
    return {viewport.map_x(logical.left),viewport.map_y(logical.top),
            viewport.map_x(logical.right),viewport.map_y(logical.bottom)};
}
void GdiChild::position(HWND window,const RECT& target) {
    RECT current{}; GetWindowRect(window,&current);
    MapWindowPoints(HWND_DESKTOP,GetParent(window),reinterpret_cast<POINT*>(&current),2);
    if(!EqualRect(&target,&current))
        SetWindowPos(window,nullptr,target.left,target.top,std::max(1L,target.right-target.left),
                     std::max(1L,target.bottom-target.top),SWP_NOZORDER|SWP_NOACTIVATE);
}
}
