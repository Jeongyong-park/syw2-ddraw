#include "overlay.h"
#include "settings_ids.h"
#include "viewport.h"
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
    for(int id:{IDC_MODE,IDC_RENDERER,IDC_STATUS}) ShowWindow(GetDlgItem(h,id),SW_HIDE);
    for(int id:{IDC_LINEAR,IDC_VSYNC,IDC_SAVE,IDC_APPLY,IDCANCEL}) {
        auto button=GetDlgItem(h,id); const auto checked=SendMessageW(button,BM_GETCHECK,0,0);
        SetWindowLongPtrW(button,GWL_STYLE,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW);
        SetWindowSubclass(button,button_proc,5,0); SendMessageW(button,BM_SETCHECK,checked,0);
    }
    struct Item{int id;const wchar_t* title;};
    for(auto i:{Item{IDC_WINDOWED,L"창 모드"},Item{IDC_FULLSCREEN,L"전체화면"},Item{IDC_GPU,L"GPU"},Item{IDC_GDI,L"GDI"}}) {
        auto button=CreateWindowW(L"BUTTON",i.title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(i.id)),GetModuleHandleW(nullptr),nullptr);
        SetWindowSubclass(button,button_proc,5,0);
    }
    SetWindowTextW(GetDlgItem(h,IDC_APPLY),L"변경 적용");
    SetWindowTextW(GetDlgItem(h,IDCANCEL),L"닫기");
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
        Item{IDC_LINEAR,494,271,568,305},Item{IDC_VSYNC,494,333,568,367},
        Item{IDC_SAVE,30,452,295,488},Item{IDCANCEL,316,452,406,488},Item{IDC_APPLY,416,452,568,488}}) {
        auto r=rect(i.a,i.b,i.c,i.d); SetWindowPos(GetDlgItem(h,i.id),nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    InvalidateRect(h,nullptr,FALSE);
}
void Overlay::paint(HWND h,HDC dc) {
    RECT client{}; GetClientRect(h,&client); fill(dc,client,RGB(9,10,11));
    if(!background.empty()) {
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=image_width;
        info.bmiHeader.biHeight=-image_height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        auto v=Viewport::fit(client.right,client.bottom,image_width,image_height);
        SetStretchBltMode(dc,COLORONCOLOR); StretchDIBits(dc,v.x,v.y,v.width,v.height,0,0,image_width,image_height,background.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    }
    fill(dc,rect(7,9,607,519),RGB(10,9,8));
    fill(dc,rect(0,0,600,510),ink); border(dc,rect(0,0,600,510),RGB(135,103,61));
    border(dc,rect(5,5,595,505),RGB(60,49,35));
    // Thin brass corner details echo the game's panels without importing game artwork.
    for(auto r:{rect(0,0,35,2),rect(0,0,2,35),rect(565,0,600,2),rect(598,0,600,35),
                rect(0,508,35,510),rect(0,475,2,510),rect(565,508,600,510),rect(598,475,600,510)}) fill(dc,r,gold);
    label(dc,small_font,rect(32,20,400,36),L"HQNET  /  DISPLAY",gold);
    label(dc,heading,rect(30,42,450,82),L"디스플레이",text);
    label(dc,small_font,rect(30,87,550,106),L"익숙한 전장, 나에게 맞는 화면.",muted);
    fill(dc,rect(30,113,570,114),RGB(76,60,40));
    label(dc,body,rect(30,126,280,148),L"화면 모드",text);
    label(dc,small_font,rect(30,150,280,169),L"화면 비율을 유지해 표시합니다",muted);
    label(dc,body,rect(30,198,280,220),L"화면 출력",text);
    label(dc,small_font,rect(30,222,288,241),L"GPU 가속 또는 GDI 호환 출력",muted);
    fill(dc,rect(30,255,570,256),RGB(57,48,36));
    label(dc,body,rect(30,270,450,292),L"부드러운 확대",text);
    label(dc,small_font,rect(30,295,450,314),L"픽셀 경계를 부드럽게 보간합니다",muted);
    label(dc,body,rect(30,333,450,355),L"수직동기화",text);
    label(dc,small_font,rect(30,358,475,377),L"게임 진행 속도에 영향을 줄 수 있습니다",muted);
    fill(dc,rect(30,394,570,395),RGB(57,48,36));
    wchar_t status[256]{}; GetDlgItemTextW(h,IDC_STATUS,status,256);
    label(dc,small_font,rect(30,405,570,439),status,muted,DT_LEFT|DT_WORDBREAK);
}
void Overlay::button(HWND h,const DRAWITEMSTRUCT& item) {
    auto r=item.rcItem; const int id=int(item.CtlID); bool selected=false;
    if(id==IDC_WINDOWED || id==IDC_FULLSCREEN) selected=(SendDlgItemMessageW(h,IDC_MODE,CB_GETCURSEL,0,0)==(id==IDC_WINDOWED?0:1));
    if(id==IDC_GPU || id==IDC_GDI) selected=(SendDlgItemMessageW(h,IDC_RENDERER,CB_GETCURSEL,0,0)==(id==IDC_GPU?0:1));
    bool toggle=id==IDC_LINEAR || id==IDC_VSYNC || id==IDC_SAVE;
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
    if(toggle && id!=IDC_SAVE) wcscpy_s(value,enabled?(selected?L"켜짐":L"꺼짐"):L"GPU 전용");
    if(id==IDC_SAVE) {
        RECT box{int(2*scale),int(10*scale),int(18*scale),int(26*scale)};
        border(item.hDC,box,selected?gold:muted);
        if(selected) { InflateRect(&box,-std::max(1,int(4*scale)),-std::max(1,int(4*scale))); fill(item.hDC,box,gold); }
        r.left+=int(28*scale); wcscpy_s(value,L"다음 실행에도 저장");
    }
    label(item.hDC,id==IDC_SAVE?small_font:body,r,value,!enabled?muted:id==IDC_APPLY?RGB(28,21,13):selected?gold:text,
          DT_VCENTER|DT_SINGLELINE|(id==IDC_SAVE?DT_LEFT:DT_CENTER));
    if(item.itemState&ODS_FOCUS) { auto focus=item.rcItem; InflateRect(&focus,-3,-3); DrawFocusRect(item.hDC,&focus); }
}
}
