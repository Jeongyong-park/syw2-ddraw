#include "overlay.h"
#include "settings_ids.h"
#include "viewport.h"
#include "syw2x.h"
#include "palette_selection.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
namespace hq {
namespace {
constexpr COLORREF gold=RGB(205,167,101), ink=RGB(26,23,21), text=RGB(239,231,214), muted=RGB(171,159,140);
void fill(HDC dc,RECT r,COLORREF color) { auto b=CreateSolidBrush(color); FillRect(dc,&r,b); DeleteObject(b); }
void border(HDC dc,RECT r,COLORREF color) { auto b=CreateSolidBrush(color); FrameRect(dc,&r,b); DeleteObject(b); }
void label(HDC dc,HFONT font,RECT r,const wchar_t* value,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
    auto old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawTextW(dc,value,-1,&r,flags|DT_NOPREFIX); SelectObject(dc,old);
}
COLORREF palette_color(uint32_t c) { return RGB((c>>16)&255,(c>>8)&255,c&255); }
bool swatch_value(HWND h,int slot,uint32_t& value) {
    wchar_t input[256]{}; GetDlgItemTextW(h,IDC_SYW2X_FIRST+palette_field(slot),input,256);
    return palette_value(input,slot,value);
}
LRESULT CALLBACK palette_proc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
    auto& overlay=*reinterpret_cast<Overlay*>(data);
    if(msg==WM_GETDLGCODE) {
        const auto key=reinterpret_cast<const MSG*>(l);
        // Keep Enter on the grid instead of invoking the dialog's save button.
        return DefSubclassProc(h,msg,w,l)|DLGC_WANTARROWS|
            (key && key->message==WM_KEYDOWN && key->wParam==VK_RETURN?DLGC_WANTMESSAGE:0);
    }
    int next=overlay.palette_index;
    if(msg==WM_KEYDOWN) {
        if(w==VK_LEFT) next=std::max(0,next-1);
        else if(w==VK_RIGHT) next=std::min(255,next+1);
        else if(w==VK_UP) next=std::max(0,next-16);
        else if(w==VK_DOWN) next=std::min(255,next+16);
        else if(w==VK_HOME) next=0;
        else if(w==VK_END) next=255;
        else if(w==VK_RETURN) { SendMessageW(GetParent(h),WM_COMMAND,IDC_PALETTE_GRID,0); return 0; }
        else return DefSubclassProc(h,msg,w,l);
    } else if(msg==WM_LBUTTONDOWN) {
        RECT r{}; GetClientRect(h,&r);
        const int px=short(LOWORD(l)),py=short(HIWORD(l));
        if(px>=0 && py>=0 && px<r.right && py<r.bottom)
            next=int(((px+1)*16-1)/r.right)+16*int(((py+1)*16-1)/r.bottom);
    }
    if(next!=overlay.palette_index) {
        overlay.palette_index=next; InvalidateRect(h,nullptr,FALSE); InvalidateRect(GetParent(h),nullptr,FALSE);
    }
    if(msg==WM_KEYDOWN) return 0;
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(h,palette_proc,6);
    return DefSubclassProc(h,msg,w,l);
}
LRESULT CALLBACK button_proc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if(msg==WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512))); return TRUE; }
    if (msg==BM_SETCHECK) { SetWindowLongPtrW(h,GWLP_USERDATA,w); InvalidateRect(h,nullptr,FALSE); return 0; }
    if (msg==BM_GETCHECK) return GetWindowLongPtrW(h,GWLP_USERDATA);
    if (msg==WM_MOUSEMOVE) { TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0}; TrackMouseEvent(&t); SetPropW(h,L"hover",reinterpret_cast<HANDLE>(1)); InvalidateRect(h,nullptr,FALSE); }
    if (msg==WM_MOUSELEAVE) { RemovePropW(h,L"hover"); InvalidateRect(h,nullptr,FALSE); }
    if (msg==WM_NCDESTROY) { RemovePropW(h,L"hover"); RemoveWindowSubclass(h,button_proc,5); }
    return DefSubclassProc(h,msg,w,l);
}
}
Overlay::~Overlay() { if(heading) DeleteObject(heading); if(body) DeleteObject(body); if(small_font) DeleteObject(small_font); }
RECT Overlay::rect(int a,int b,int c,int d) const { return {x+int(std::lround(a*scale)),y+int(std::lround(b*scale)),x+int(std::lround(c*scale)),y+int(std::lround(d*scale))}; }
void Overlay::init(HWND h) {
    syw2x_page=false;
    palette_target=-1;
    for(int id:{IDC_MODE,IDC_RENDERER,IDC_SCALING,IDC_STATUS}) ShowWindow(GetDlgItem(h,id),SW_HIDE);
    for(int id:{IDC_VSYNC,IDC_SAVE,IDC_APPLY,IDCANCEL}) {
        auto button=GetDlgItem(h,id); const auto checked=SendMessageW(button,BM_GETCHECK,0,0);
        SetWindowLongPtrW(button,GWL_STYLE,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW);
        SetWindowSubclass(button,button_proc,5,0); SendMessageW(button,BM_SETCHECK,checked,0);
    }
    struct Item{int id;const wchar_t* title;};
    for(auto i:{Item{IDC_WINDOWED,L"창 모드"},Item{IDC_FULLSCREEN,L"전체화면"},Item{IDC_GPU,L"GPU"},Item{IDC_GDI,L"GDI"},Item{IDC_NEAREST,L"Nearest"},Item{IDC_BILINEAR,L"Bilinear"},Item{IDC_SHARP,L"Sharp Bilinear"},Item{IDC_INTEGER,L"Integer"}}) {
        auto button=CreateWindowW(L"BUTTON",i.title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(i.id)),GetModuleHandleW(nullptr),nullptr);
        SetWindowSubclass(button,button_proc,5,0);
    }
    SetWindowTextW(GetDlgItem(h,IDC_APPLY),L"변경 적용");
    SetWindowTextW(GetDlgItem(h,IDCANCEL),L"닫기");
    for(auto i:{Item{IDC_TAB_DISPLAY,L"디스플레이"},Item{IDC_TAB_SYW2X,L"SYW2X"}}) {
        auto button=CreateWindowW(L"BUTTON",i.title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(i.id)),GetModuleHandleW(nullptr),nullptr);
        SetWindowSubclass(button,button_proc,5,0);
    }
    for(int i=0;i<12;++i) {
        auto control=CreateWindowExW(i<4?0:WS_EX_CLIENTEDGE,i<4?L"BUTTON":L"EDIT",i<4?syw2x_options[i].label:L"",
            WS_CHILD|WS_TABSTOP|(i<4?BS_OWNERDRAW:ES_AUTOHSCROLL),0,0,1,1,h,
            reinterpret_cast<HMENU>(INT_PTR(IDC_SYW2X_FIRST+i)),GetModuleHandleW(nullptr),nullptr);
        if(i<4) SetWindowSubclass(control,button_proc,5,0);
        else SendMessageW(control,EM_SETLIMITTEXT,32,0);
        EnableWindow(control,syw2x_available);
    }
    for(int slot=0;slot<11;++slot) {
        const std::wstring title=std::wstring(syw2x_options[palette_field(slot)].label)+L" 색상 선택 "+std::to_wstring(slot<7?1:slot-6);
        auto control=CreateWindowW(L"BUTTON",title.c_str(),WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,h,
            reinterpret_cast<HMENU>(INT_PTR(IDC_SWATCH_FIRST+slot)),GetModuleHandleW(nullptr),nullptr);
        SetWindowSubclass(control,button_proc,5,0);
    }
    auto grid=CreateWindowW(L"BUTTON",L"SYW2X 팔레트: 방향키로 이동, Enter 또는 Space로 선택",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,
        0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(IDC_PALETTE_GRID)),GetModuleHandleW(nullptr),nullptr);
    SetWindowSubclass(grid,palette_proc,6,reinterpret_cast<DWORD_PTR>(this));
    // Resource static labels are replaced by the precisely laid out painted typography.
    for(HWND c=GetWindow(h,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)) if(GetDlgCtrlID(c)==-1) ShowWindow(c,SW_HIDE);
    layout(h);
}
void Overlay::layout(HWND h) {
    RECT client{}; GetClientRect(GetParent(h),&client);
    SetWindowPos(h,HWND_TOP,0,0,client.right,client.bottom,SWP_NOACTIVATE);
    scale=std::max(.1f,std::min(client.right/800.f,client.bottom/600.f));
    x=(client.right-int(600*scale))/2; y=(client.bottom-int(510*scale))/2;
    auto font=[&](int size,int weight) { return CreateFontW(-std::max(1,int(size*scale)),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"맑은 고딕"); };
    if(heading) DeleteObject(heading); if(body) DeleteObject(body); if(small_font) DeleteObject(small_font);
    heading=font(30,FW_BOLD); body=font(15,FW_MEDIUM); small_font=font(12,FW_NORMAL);
    struct Item{int id,a,b,c,d;};
    for(auto i:{Item{IDC_WINDOWED,300,125,431,164},Item{IDC_FULLSCREEN,437,125,568,164},
        Item{IDC_GPU,300,197,431,236},Item{IDC_GDI,437,197,568,236},
        Item{IDC_NEAREST,300,265,431,292},Item{IDC_BILINEAR,437,265,568,292},
        Item{IDC_SHARP,300,298,431,325},Item{IDC_INTEGER,437,298,568,325},Item{IDC_VSYNC,494,333,568,367},
        Item{IDC_SAVE,30,452,295,488},Item{IDCANCEL,316,452,406,488},Item{IDC_APPLY,416,452,568,488},
        Item{IDC_TAB_DISPLAY,370,42,470,78},Item{IDC_TAB_SYW2X,478,42,570,78}}) {
        auto r=rect(i.a,i.b,i.c,i.d); SetWindowPos(GetDlgItem(h,i.id),nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    for(int id:{IDC_WINDOWED,IDC_FULLSCREEN,IDC_GPU,IDC_GDI,IDC_NEAREST,IDC_BILINEAR,IDC_SHARP,IDC_INTEGER,IDC_VSYNC,IDC_SAVE})
        ShowWindow(GetDlgItem(h,id),syw2x_page?SW_HIDE:SW_SHOWNA);
    for(int i=0;i<12;++i) {
        const int col=i<4?i%2:(i-4)%2, row=i<4?i/2:(i-4)/2;
        auto r= i<4?rect(230+col*280,128+row*43,290+col*280,160+row*43):
            rect(112+col*280,243+row*34,234+col*280,272+row*34);
        auto control=GetDlgItem(h,IDC_SYW2X_FIRST+i);
        SetWindowPos(control,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(small_font),FALSE);
        ShowWindow(control,syw2x_page && palette_target<0?SW_SHOWNA:SW_HIDE);
    }
    for(int slot=0;slot<11;++slot) {
        const int i=palette_field(slot),col=(i-4)%2,row=(i-4)/2;
        const int left=239+col*280+(slot<7?0:(slot-7)*13);
        const auto r=rect(left,243+row*34,left+(slot<7?49:12),272+row*34);
        auto control=GetDlgItem(h,IDC_SWATCH_FIRST+slot);
        SetWindowPos(control,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
        ShowWindow(control,syw2x_page && palette_target<0?SW_SHOWNA:SW_HIDE);
    }
    auto grid=GetDlgItem(h,IDC_PALETTE_GRID); auto grid_rect=rect(30,122,350,442);
    SetWindowPos(grid,nullptr,grid_rect.left,grid_rect.top,grid_rect.right-grid_rect.left,grid_rect.bottom-grid_rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(grid,palette_target>=0?SW_SHOWNA:SW_HIDE);
    ShowWindow(GetDlgItem(h,IDC_APPLY),palette_target<0?SW_SHOWNA:SW_HIDE);
    SetWindowTextW(GetDlgItem(h,IDCANCEL),palette_target<0?L"닫기":L"뒤로");
    refresh_swatches(h);
    SetWindowTextW(GetDlgItem(h,IDC_APPLY),syw2x_page?L"저장 · 재시작 후 적용":L"변경 적용");
    EnableWindow(GetDlgItem(h,IDC_APPLY),!syw2x_page || syw2x_available);
    InvalidateRect(h,nullptr,FALSE);
}
void Overlay::paint(HWND h,HDC dc) {
    RECT client{}; GetClientRect(h,&client); fill(dc,client,RGB(9,10,11));
    if(!background.empty()) {
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=image_width;
        info.bmiHeader.biHeight=-image_height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        auto v=Viewport::fit(client.right,client.bottom,image_width,image_height,integer_scaling);
        SetStretchBltMode(dc,COLORONCOLOR); StretchDIBits(dc,v.x,v.y,v.width,v.height,0,0,image_width,image_height,background.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    }
    fill(dc,rect(7,9,607,519),RGB(10,9,8));
    fill(dc,rect(0,0,600,510),ink); border(dc,rect(0,0,600,510),RGB(135,103,61));
    border(dc,rect(5,5,595,505),RGB(60,49,35));
    // Thin brass corner details echo the game's panels without importing game artwork.
    for(auto r:{rect(0,0,35,2),rect(0,0,2,35),rect(565,0,600,2),rect(598,0,600,35),
                rect(0,508,35,510),rect(0,475,2,510),rect(565,508,600,510),rect(598,475,600,510)}) fill(dc,r,gold);
    label(dc,small_font,rect(32,20,400,36),L"HQNET  /  SETTINGS",gold);
    label(dc,heading,rect(30,42,350,82),syw2x_page?L"SYW2X":L"디스플레이",text);
    label(dc,small_font,rect(30,87,570,106),syw2x_page?syw2x_state.c_str():L"익숙한 전장, 나에게 맞는 화면.",muted);
    fill(dc,rect(30,113,570,114),RGB(76,60,40));
    if(syw2x_page) {
        if(palette_target>=0) {
            label(dc,body,rect(367,130,570,160),syw2x_options[palette_field(palette_target)].label,text);
            if(palette_target>=7) {
                const auto slot=L"16진수 왼쪽부터 "+std::to_wstring(palette_target-6)+L"번째 색";
                label(dc,small_font,rect(367,160,570,185),slot.c_str(),muted);
            }
            fill(dc,rect(367,195,570,250),palette_color(palette[palette_index]));
            wchar_t value[96]{}; const auto c=palette[palette_index];
            swprintf_s(value,L"%d / 0x%02X\nRGB %d, %d, %d",palette_index,palette_index,(c>>16)&255,(c>>8)&255,c&255);
            label(dc,small_font,rect(367,265,570,310),value,text,DT_LEFT|DT_WORDBREAK);
            label(dc,small_font,rect(367,325,570,433),L"클릭 또는 방향키 + Enter\nEsc: 선택 취소\n\nSYW2X 설정기 기준 색상입니다. 게임 장면에 따라 실제 색은 달라질 수 있습니다.",muted,DT_LEFT|DT_WORDBREAK);
            return;
        }
        for(int i=0;i<12;++i) {
            const int col=i<4?i%2:(i-4)%2,row=i<4?i/2:(i-4)/2;
            auto r=i<4?rect(30+col*280,128+row*43,225+col*280,160+row*43):
                rect(30+col*280,243+row*34,110+col*280,272+row*34);
            label(dc,small_font,r,i==11?L"유닛 4색":syw2x_options[i].label,text);
        }
        label(dc,small_font,rect(30,214,570,236),L"SYW2X 기준 색상 · 견본을 눌러 선택 · 4색은 왼쪽부터",muted);
        fill(dc,rect(30,394,570,395),RGB(57,48,36));
        label(dc,small_font,rect(30,405,570,439),syw2x_status.c_str(),muted,DT_LEFT|DT_WORDBREAK);
        label(dc,small_font,rect(30,452,302,488),L"적용하려면 게임을 다시 실행하세요.",muted);
        return;
    }
    label(dc,body,rect(30,126,280,148),L"화면 모드",text);
    label(dc,small_font,rect(30,150,280,169),L"화면 비율을 유지해 표시합니다",muted);
    label(dc,body,rect(30,198,280,220),L"화면 출력",text);
    label(dc,small_font,rect(30,222,288,241),L"GPU 가속 또는 GDI 호환 출력",muted);
    fill(dc,rect(30,255,570,256),RGB(57,48,36));
    label(dc,body,rect(30,270,288,292),L"업스케일 방식",text);
    label(dc,small_font,rect(30,295,288,314),L"보간 필터는 GPU 출력에서 사용",muted);
    label(dc,body,rect(30,333,450,355),L"수직동기화",text);
    label(dc,small_font,rect(30,358,475,377),L"게임 진행 속도에 영향을 줄 수 있습니다",muted);
    fill(dc,rect(30,394,570,395),RGB(57,48,36));
    wchar_t status[256]{}; GetDlgItemTextW(h,IDC_STATUS,status,256);
    label(dc,small_font,rect(30,405,570,439),status,muted,DT_LEFT|DT_WORDBREAK);
}
void Overlay::button(HWND h,const DRAWITEMSTRUCT& item) {
    auto r=item.rcItem; const int id=int(item.CtlID); bool selected=false;
    if(id==IDC_PALETTE_GRID) {
        for(int i=0;i<256;++i) {
            RECT cell{r.left+(i%16)*(r.right-r.left)/16,r.top+(i/16)*(r.bottom-r.top)/16,
                r.left+(i%16+1)*(r.right-r.left)/16,r.top+(i/16+1)*(r.bottom-r.top)/16};
            fill(item.hDC,cell,palette_color(palette[i]));
            border(item.hDC,cell,RGB(26,23,21));
            wchar_t code[3]{}; swprintf_s(code,L"%02X",i);
            const auto c=palette[i];
            const auto contrast=299*((c>>16)&255)+587*((c>>8)&255)+114*(c&255)>140000?RGB(0,0,0):RGB(255,255,255);
            label(item.hDC,small_font,cell,code,contrast,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(i==palette_index) {
                InflateRect(&cell,-1,-1); border(item.hDC,cell,RGB(255,255,255));
                InflateRect(&cell,-1,-1); border(item.hDC,cell,RGB(0,0,0));
            }
        }
        return;
    }
    if(id>=IDC_SWATCH_FIRST && id<IDC_SWATCH_FIRST+11) {
        uint32_t value=0; const int slot=id-IDC_SWATCH_FIRST;
        if(swatch_value(h,slot,value)) fill(item.hDC,r,palette_color(palette[palette_byte(value,slot)]));
        else { fill(item.hDC,r,ink); label(item.hDC,small_font,r,L"?",muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
        border(item.hDC,r,gold);
        if(item.itemState&ODS_FOCUS) { InflateRect(&r,-3,-3); DrawFocusRect(item.hDC,&r); }
        return;
    }
    if(id==IDC_WINDOWED || id==IDC_FULLSCREEN) selected=(SendDlgItemMessageW(h,IDC_MODE,CB_GETCURSEL,0,0)==(id==IDC_WINDOWED?0:1));
    if(id==IDC_GPU || id==IDC_GDI) selected=(SendDlgItemMessageW(h,IDC_RENDERER,CB_GETCURSEL,0,0)==(id==IDC_GPU?0:1));
    if(id>=IDC_NEAREST && id<=IDC_INTEGER) selected=SendDlgItemMessageW(h,IDC_SCALING,CB_GETCURSEL,0,0)==id-IDC_NEAREST;
    if(id==IDC_TAB_DISPLAY || id==IDC_TAB_SYW2X) selected=syw2x_page==(id==IDC_TAB_SYW2X);
    bool toggle=id==IDC_VSYNC || id==IDC_SAVE || (id>=IDC_SYW2X_FIRST && id<IDC_SYW2X_FIRST+4);
    if(toggle) selected=SendMessageW(item.hwndItem,BM_GETCHECK,0,0)==BST_CHECKED;
    bool enabled=IsWindowEnabled(item.hwndItem)!=FALSE, hover=GetPropW(item.hwndItem,L"hover")!=nullptr;
    COLORREF bg=selected?RGB(79,57,31):RGB(39,34,28);
    if(hover) bg=selected?RGB(103,74,39):RGB(57,47,35);
    if(item.itemState&ODS_SELECTED) bg=RGB(64,43,23);
    if(id==IDC_APPLY) bg=(item.itemState&ODS_SELECTED)?RGB(140,104,54):hover?RGB(222,183,115):gold;
    if(id==IDC_SAVE) bg=ink;
    fill(item.hDC,r,bg);
    if(id!=IDC_SAVE) border(item.hDC,r,selected?gold:RGB(88,70,47));
    wchar_t value[96]{}; GetWindowTextW(item.hwndItem,value,96);
    if(toggle && id!=IDC_SAVE) wcscpy_s(value,enabled?(selected?L"켜짐":L"꺼짐"):id==IDC_VSYNC?L"GPU 전용":L"없음");
    if(id==IDC_SAVE) {
        RECT box{int(2*scale),int(10*scale),int(18*scale),int(26*scale)};
        border(item.hDC,box,selected?gold:muted);
        if(selected) { InflateRect(&box,-std::max(1,int(4*scale)),-std::max(1,int(4*scale))); fill(item.hDC,box,gold); }
        r.left+=int(28*scale); wcscpy_s(value,L"다음 실행에도 저장");
    }
    label(item.hDC,id==IDC_SAVE || (syw2x_page && id==IDC_APPLY)?small_font:body,r,value,!enabled?muted:id==IDC_APPLY?RGB(28,21,13):selected?gold:text,
          DT_VCENTER|DT_SINGLELINE|(id==IDC_SAVE?DT_LEFT:DT_CENTER));
    if(item.itemState&ODS_FOCUS) { auto focus=item.rcItem; InflateRect(&focus,-3,-3); DrawFocusRect(item.hDC,&focus); }
}
void Overlay::refresh_swatches(HWND h) {
    for(int slot=0;slot<11;++slot) {
        uint32_t value=0; auto control=GetDlgItem(h,IDC_SWATCH_FIRST+slot);
        EnableWindow(control,syw2x_available && swatch_value(h,slot,value));
        InvalidateRect(control,nullptr,FALSE);
    }
}
bool Overlay::palette_command(HWND h,int id,int notification) {
    if(id>=IDC_SYW2X_FIRST+4 && id<IDC_SYW2X_FIRST+12 && notification==EN_CHANGE) {
        refresh_swatches(h); return true;
    }
    if(notification!=BN_CLICKED) return false;
    if(palette_target>=0 && id==IDC_APPLY) return true;
    if(id==IDC_TAB_DISPLAY || id==IDC_TAB_SYW2X) palette_target=-1;
    if(id>=IDC_SWATCH_FIRST && id<IDC_SWATCH_FIRST+11) {
        const int slot=id-IDC_SWATCH_FIRST; uint32_t value=0;
        if(!syw2x_page || !syw2x_available || !swatch_value(h,slot,value)) return true;
        palette_target=slot; palette_index=palette_byte(value,slot);
        layout(h); SetFocus(GetDlgItem(h,IDC_PALETTE_GRID)); return true;
    }
    if(palette_target>=0 && (id==IDCANCEL || id==IDC_PALETTE_GRID || id==IDOK)) {
        const int slot=palette_target;
        if(id!=IDCANCEL) {
            uint32_t value=0;
            if(syw2x_available && swatch_value(h,slot,value)) {
                wchar_t input[32]{};
                swprintf_s(input,slot<7?L"0x%02X":L"0x%08X",palette_replace(value,slot,palette_index));
                SetDlgItemTextW(h,IDC_SYW2X_FIRST+palette_field(slot),input);
            }
        }
        palette_target=-1; layout(h); SetFocus(GetDlgItem(h,IDC_SWATCH_FIRST+slot)); return true;
    }
    return false;
}
}
