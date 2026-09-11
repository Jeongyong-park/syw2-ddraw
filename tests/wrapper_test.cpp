#include <windows.h>
#include <ddraw.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "../src/pixels.h"
#include "../src/viewport.h"
#include "../src/scaling.h"
#include "../src/settings_ids.h"

// Do not use assert: Release builds must execute every check.
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
#define OK(x) CHECK((x)==DD_OK)
int production_keys=0;
LPARAM last_mouse=0;
void check_covered(HWND child) {
    // The full-client settings overlay must clip even direct GDI writes to EDITs.
    CHECK(IsWindowVisible(child));
    RedrawWindow(child,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    HDC dc=GetDC(child); CHECK(dc);
    RECT visible{};
    const int region=GetClipBox(dc,&visible);
    ReleaseDC(child,dc);
    CHECK(region==NULLREGION);
}
LRESULT CALLBACK game_proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_MOUSEMOVE) { last_mouse=l; return 0; }
    if(w==VK_F10 && (msg==WM_KEYDOWN || msg==WM_KEYUP || msg==WM_SYSKEYDOWN || msg==WM_SYSKEYUP)) { ++production_keys; return 0; }
    return DefWindowProcW(h,msg,w,l);
}
void pixels() {
    auto wide=hq::Viewport::fit(1920,1080,800,600);
    CHECK(wide.x==240 && wide.y==0 && wide.width==1440 && wide.height==1080);
    CHECK(wide.map_x(400)==960 && wide.map_y(300)==540);
    CHECK(wide.unmap_x(960)==400 && wide.unmap_y(540)==300);
    auto tall=hq::Viewport::fit(600,1000,800,600);
    CHECK(tall.x==0 && tall.y==275 && tall.width==600 && tall.height==450);
    CHECK(tall.unmap_y(tall.map_y(200))==200);
    auto integer=hq::Viewport::fit(1920,1080,800,600,true);
    CHECK(integer.width==800 && integer.height==600 && integer.x==560 && integer.y==240);
    CHECK(integer.unmap_x(integer.map_x(300))==300);
    auto big=hq::Viewport::fit(3840,2160,800,600,true);
    CHECK(big.width==2400 && big.height==1800);
    auto reduced=hq::Viewport::fit(400,300,800,600,true);
    CHECK(reduced.width==400 && reduced.height==300);
    CHECK(hq::parse_scaling(L"",true)==hq::Bilinear);
    CHECK(hq::parse_scaling(L"",false)==hq::Nearest); // explicit legacy LinearFilter=0
    CHECK(hq::parse_scaling(L"",false,false)==hq::SharpBilinear); // no scaling keys
    CHECK(hq::parse_scaling(L"nearest",false,false)==hq::Nearest); // explicit choice wins
    CHECK(hq::parse_scaling(L"integer",true)==hq::Integer);
    CHECK(hq::parse_scaling(L"bad",true)==hq::Nearest);
    auto tiny=hq::Viewport::fit(0,0,800,600); CHECK(tiny.width>0 && tiny.height>0);
    hq::Palette p{}; p[1]=0xff0000; p[255]=0x123456;
    hq::Image a(3,2,8); CHECK(a.pitch==4);
    a.write(0,0,1); a.write(2,1,255);
    auto rgb=hq::rgb(a,p); CHECK(rgb[0]==0xff0000); CHECK(rgb[5]==0x123456);
    p[1]=0x00ff00; CHECK(hq::rgb(a,p)[0]==0x00ff00);
    hq::Image b(3,1,16); b.write(0,0,0xf800); b.write(1,0,0x07e0); b.write(2,0,0x001f);
    rgb=hq::rgb(b,p); CHECK(rgb[0]==0xff0000 && rgb[1]==0x00ff00 && rgb[2]==0x0000ff);
    hq::Image source(4,1,8), dest(4,1,8);
    for(int i=0;i<4;++i) source.write(i,0,i+1);
    hq::fill(dest,{0,0,4,1},9);
    hq::blit(dest,{-2,0,6,1},source,{0,0,4,1},true,2,2,false,0,0);
    CHECK(dest.read(0,0)==9 && dest.read(1,0)==9 && dest.read(2,0)==3 && dest.read(3,0)==3);
    hq::blit(source,{1,0,4,1},source,{0,0,3,1},false,0,0,false,0,0);
    CHECK(source.read(1,0)==1 && source.read(2,0)==2 && source.read(3,0)==3);
    hq::blit(dest,{0,0,4,1},source,{0,0,4,1},false,0,0,false,0,0,true,false);
    CHECK(dest.read(0,0)==3 && dest.read(3,0)==1);
    hq::fill(dest,{-100,-100,100,100},7); CHECK(dest.read(3,0)==7);
    CHECK(!hq::valid_rect({INT_MIN,0,INT_MAX,1}));
    bool threw=false; try { hq::Image invalid(-1,3,8); } catch (const std::invalid_argument&) { threw=true; }
    CHECK(threw);
}
int wmain(int argc, wchar_t** argv) {
    CHECK(argc==2 || (argc==3 && wcscmp(argv[2],L"--save-test")==0)); pixels();
    auto dll=LoadLibraryW(argv[1]); CHECK(dll);
    using Create=HRESULT (WINAPI*)(GUID*,void**,REFIID,IUnknown*);
    auto create=reinterpret_cast<Create>(GetProcAddress(dll,"DirectDrawCreateEx")); CHECK(create);
    IDirectDraw7* d=nullptr;
    OK(create(nullptr,reinterpret_cast<void**>(&d),IID_IDirectDraw7,nullptr));
    void* unknown=nullptr; OK(d->QueryInterface(IID_IUnknown,&unknown)); CHECK(unknown==d); static_cast<IUnknown*>(unknown)->Release();
    unknown=reinterpret_cast<void*>(1); CHECK(d->QueryInterface(IID_IDirectDraw4,&unknown)==E_NOINTERFACE && unknown==nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc=game_proc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"HQCDD_TEST";
    CHECK(RegisterClassW(&wc));
    auto window=CreateWindowW(wc.lpszClassName,L"HQCDD integration test",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,wc.hInstance,nullptr);
    CHECK(window);
    OK(d->SetCooperativeLevel(window,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN));
    OK(d->SetDisplayMode(64,48,8,0,0));
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(-1,-1));
    CHECK(last_mouse==MAKELPARAM(0,0));
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(16000,16000));
    CHECK(last_mouse==MAKELPARAM(63,47));
    ShowWindow(window,SW_HIDE);
    DDSURFACEDESC2 mode{}; mode.dwSize=sizeof(mode); OK(d->GetDisplayMode(&mode)); CHECK(mode.dwWidth==64 && mode.ddpfPixelFormat.dwRGBBitCount==8);
    DDSURFACEDESC2 desc{}; desc.dwSize=sizeof(desc); desc.dwFlags=DDSD_CAPS|DDSD_BACKBUFFERCOUNT;
    desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_FLIP|DDSCAPS_COMPLEX; desc.dwBackBufferCount=1;
    IDirectDrawSurface7* front=nullptr; OK(d->CreateSurface(&desc,&front,nullptr));
    IDirectDrawSurface7* other=nullptr; CHECK(d->CreateSurface(&desc,&other,nullptr)==DDERR_PRIMARYSURFACEALREADYEXISTS && !other);
    DDSCAPS2 cap{}; cap.dwCaps=DDSCAPS_BACKBUFFER;
    IDirectDrawSurface7* back=nullptr; OK(front->GetAttachedSurface(&cap,&back));
    PALETTEENTRY entries[256]{}; entries[1]={255,0,0,0}; entries[2]={0,255,0,0};
    IDirectDrawPalette* pal=nullptr; OK(d->CreatePalette(DDPCAPS_8BIT|DDPCAPS_ALLOW256,entries,&pal,nullptr));
    OK(front->SetPalette(pal)); OK(back->SetPalette(pal));
    DDSURFACEDESC2 locked{}; locked.dwSize=sizeof(locked);
    OK(back->Lock(nullptr,&locked,DDLOCK_WAIT,nullptr));
    CHECK(locked.lPitch==64 && locked.dwWidth==64);
    static_cast<unsigned char*>(locked.lpSurface)[0]=1;
    CHECK(front->Flip(nullptr,DDFLIP_WAIT)==DDERR_SURFACEBUSY);
    OK(back->Unlock(nullptr)); CHECK(back->Unlock(nullptr)==DDERR_NOTLOCKED);
    OK(front->Flip(nullptr,DDFLIP_WAIT));
    // Partial writable unlock requests its locked area; readonly unlock requests no output.
    RECT diagnostic_area{8,8,12,11}; locked={}; locked.dwSize=sizeof(locked);
    OK(front->Lock(&diagnostic_area,&locked,DDLOCK_WAIT,nullptr)); OK(front->Unlock(nullptr));
    locked={}; locked.dwSize=sizeof(locked);
    OK(front->Lock(&diagnostic_area,&locked,DDLOCK_WAIT|DDLOCK_READONLY,nullptr)); OK(front->Unlock(nullptr));
    RECT diagnostic_source{0,0,2,2};
    OK(front->BltFast(20,20,back,&diagnostic_source,DDBLTFAST_WAIT));
    RECT diagnostic_clip{63,47,70,60}; DDBLTFX diagnostic_fill{}; diagnostic_fill.dwSize=sizeof(diagnostic_fill);
    OK(front->Blt(&diagnostic_clip,nullptr,nullptr,DDBLT_COLORFILL,&diagnostic_fill));
    OK(front->Lock(nullptr,&locked,DDLOCK_READONLY,nullptr)); CHECK(static_cast<unsigned char*>(locked.lpSurface)[0]==1); OK(front->Unlock(nullptr));
    PALETTEENTRY change{0,0,255,0}; OK(pal->SetEntries(0,1,1,&change));
    PALETTEENTRY got{}; OK(pal->GetEntries(0,1,1,&got)); CHECK(got.peBlue==255);
    CHECK(pal->SetEntries(0,255,2,&change)==DDERR_INVALIDPARAMS);
    // The GDI surface view must use the wrapper's own palette, not desktop colors.
    HDC dc=nullptr; OK(front->GetDC(&dc)); CHECK(GetPixel(dc,0,0)==RGB(0,0,255));
    change={255,0,0,0}; OK(pal->SetEntries(0,1,1,&change)); CHECK(GetPixel(dc,0,0)==RGB(255,0,0));
    SetPixelV(dc,1,0,RGB(0,255,0)); OK(front->ReleaseDC(dc));
    OK(front->Lock(nullptr,&locked,0,nullptr)); CHECK(static_cast<unsigned char*>(locked.lpSurface)[1]==2); OK(front->Unlock(nullptr));
    DDBLTFX fx{}; fx.dwSize=sizeof(fx); fx.dwFillColor=2;
    OK(back->Blt(nullptr,nullptr,nullptr,DDBLT_COLORFILL|DDBLT_WAIT,&fx));
    OK(front->BltFast(0,0,back,nullptr,DDBLTFAST_WAIT));
    OK(front->Lock(nullptr,&locked,0,nullptr)); CHECK(static_cast<unsigned char*>(locked.lpSurface)[0]==2); OK(front->Unlock(nullptr));
    RECT bad{-1,0,10,10}; CHECK(front->Lock(&bad,&locked,0,nullptr)==DDERR_INVALIDRECT);
    IDirectDrawClipper* clipper=nullptr; OK(d->CreateClipper(0,&clipper,nullptr));
    OK(clipper->SetHWnd(0,window)); OK(front->SetClipper(clipper));
    POINT pt{}; CHECK(ClientToScreen(window,&pt)); RECT screen{pt.x,pt.y,pt.x+64,pt.y+48};
    fx.dwFillColor=1; OK(back->Blt(nullptr,nullptr,nullptr,DDBLT_COLORFILL,&fx));
    OK(front->Blt(&screen,back,nullptr,DDBLT_WAIT,nullptr));
    OK(front->Lock(nullptr,&locked,0,nullptr)); CHECK(static_cast<unsigned char*>(locked.lpSurface)[0]==1); OK(front->Unlock(nullptr));
    CHECK(front->BltFast(0,0,back,nullptr,0)==DDERR_BLTFASTCANTCLIP);
    // A native login EDIT must survive full primary repaints and palette changes.
    auto edit=CreateWindowW(L"EDIT",L"HQNET",WS_CHILD|WS_VISIBLE|WS_BORDER,4,4,50,22,window,nullptr,wc.hInstance,nullptr);
    CHECK(edit); ShowWindow(window,SW_SHOWNORMAL); UpdateWindow(window); UpdateWindow(edit);
    HDC childdc=::GetDC(edit); const COLORREF before=GetPixel(childdc,2,2); ::ReleaseDC(edit,childdc);
    CHECK(before!=CLR_INVALID);
    for (int i=0;i<20;++i) { OK(front->Blt(nullptr,back,nullptr,DDBLT_WAIT,nullptr)); OK(pal->SetEntries(0,1,1,&change)); }
    GdiFlush(); childdc=::GetDC(edit); CHECK(GetPixel(childdc,2,2)==before); ::ReleaseDC(edit,childdc);
    // Fullscreen uses the current monitor, preserves the DD mode, and restores placement.
    RECT old_window{}; GetWindowRect(window,&old_window);
    MONITORINFO monitor{sizeof(monitor)}; CHECK(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor));
    constexpr LPARAM alt_enter=(LPARAM(1)<<29)|1;
    SendMessageW(window,WM_SYSKEYDOWN,VK_RETURN,alt_enter);
    RECT full{}; GetWindowRect(window,&full); CHECK(EqualRect(&full,&monitor.rcMonitor));
    CHECK((GetWindowLongW(window,GWL_STYLE)&WS_CAPTION)==0);
    OK(d->GetDisplayMode(&mode)); CHECK(mode.dwWidth==64 && mode.dwHeight==48);
    SendMessageW(window,WM_SYSKEYDOWN,VK_RETURN,alt_enter|(LPARAM(1)<<30));
    CHECK((GetWindowLongW(window,GWL_STYLE)&WS_CAPTION)==0); // key autorepeat must not toggle
    SendMessageW(window,WM_SYSKEYUP,VK_RETURN,alt_enter|(LPARAM(1)<<31));
    RECT client{}; GetClientRect(window,&client);
    auto v=hq::Viewport::fit(client.right,client.bottom,64,48);
    RECT childrect{}; GetWindowRect(edit,&childrect); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&childrect),2);
    CHECK(childrect.left==v.map_x(4) && childrect.top==v.map_y(4));
    CHECK(childrect.right==v.map_x(54) && childrect.bottom==v.map_y(26));
    // A game MoveWindow call after entering fullscreen still uses logical pixels.
    CHECK(MoveWindow(edit,6,5,40,20,TRUE));
    GetWindowRect(edit,&childrect); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&childrect),2);
    CHECK(childrect.left==v.map_x(6) && childrect.right==v.map_x(46));
    wchar_t text[32]{}; GetWindowTextW(edit,text,32); CHECK(wcscmp(text,L"HQNET")==0);
    for (int i=0;i<5;++i) OK(front->Blt(nullptr,back,nullptr,DDBLT_WAIT,nullptr));
    GetWindowTextW(edit,text,32); CHECK(wcscmp(text,L"HQNET")==0);
    // Child focus receives Alt+Enter instead of the top-level window.
    SetFocus(edit); SendMessageW(edit,WM_SYSKEYDOWN,VK_RETURN,alt_enter);
    SendMessageW(edit,WM_SYSKEYUP,VK_RETURN,alt_enter|(LPARAM(1)<<31));
    RECT restored{}; GetWindowRect(window,&restored); CHECK(EqualRect(&restored,&old_window));
    CHECK(GetFocus()==edit);
    GetClientRect(window,&client); v=hq::Viewport::fit(client.right,client.bottom,64,48);
    GetWindowRect(edit,&childrect); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&childrect),2);
    CHECK(childrect.left==v.map_x(6) && childrect.right==v.map_x(46));
    // Resize and letterbox while keeping the logical framebuffer unchanged.
    SetWindowPos(window,nullptr,0,0,700,400,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    GetClientRect(window,&client); v=hq::Viewport::fit(client.right,client.bottom,64,48);
    GetWindowRect(edit,&childrect); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&childrect),2);
    CHECK(childrect.left==v.map_x(6) && childrect.top==v.map_y(5));
    OK(d->GetDisplayMode(&mode)); CHECK(mode.dwWidth==64 && mode.dwHeight==48);
    // Child controls created AFTER switching must use the same transform.
    auto late=CreateWindowW(L"EDIT",L"late",WS_CHILD|WS_VISIBLE,10,10,20,12,window,nullptr,wc.hInstance,nullptr);
    CHECK(late); OK(front->Blt(nullptr,back,nullptr,DDBLT_WAIT,nullptr));
    GetWindowRect(late,&childrect); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&childrect),2);
    CHECK(childrect.left==v.map_x(10) && childrect.right==v.map_x(30));
    DestroyWindow(late);
    // Minimize/restore cannot lose software surfaces or input text.
    ShowWindow(window,SW_MINIMIZE); OK(front->IsLost()); ShowWindow(window,SW_RESTORE);
    GetWindowTextW(edit,text,32); CHECK(wcscmp(text,L"HQNET")==0);
    // Modeless settings open from focused game controls and apply without a restart.
    GetSystemMenu(window,TRUE); // game/Windows can recreate this menu during startup
    auto menu=GetSystemMenu(window,FALSE);
    SendMessageW(window,WM_INITMENU,reinterpret_cast<WPARAM>(menu),0);
    CHECK(GetMenuState(menu,0x1e30,MF_BYCOMMAND)!=UINT(-1));
    // F10 is a production key: preserve both queue and original game procedure delivery.
    for(UINT message:{WM_KEYDOWN,WM_KEYUP,WM_SYSKEYDOWN,WM_SYSKEYUP}) {
        PostMessageW(window,message,VK_F10,0);
        MSG key{}; CHECK(GetMessageW(&key,window,message,message)>0);
        CHECK(key.message==message && key.wParam==VK_F10);
        DispatchMessageW(&key);
    }
    CHECK(production_keys==4 && !FindWindowExW(window,nullptr,L"#32770",nullptr));
    auto hidden=CreateWindowW(L"EDIT",L"hidden",WS_CHILD,0,0,10,10,window,nullptr,wc.hInstance,nullptr);
    auto clipped=CreateWindowW(L"EDIT",L"clipped",WS_CHILD|WS_CLIPSIBLINGS,0,0,10,10,window,nullptr,wc.hInstance,nullptr);
    CHECK(hidden && clipped);
    CHECK(!(GetWindowLongPtrW(edit,GWL_STYLE)&WS_CLIPSIBLINGS));
    CHECK(!(GetWindowLongPtrW(hidden,GWL_STYLE)&WS_CLIPSIBLINGS));
    SetFocus(edit);
    SendMessageW(edit,EM_SETSEL,1,4);
    const auto prior_cursor=SetCursor(nullptr);
    const int initial_cursor_count=ShowCursor(TRUE)-1; ShowCursor(FALSE);
    int hidden_cursor_count=ShowCursor(FALSE);
    while(hidden_cursor_count>-3) hidden_cursor_count=ShowCursor(FALSE);
    BYTE original_keys[256]{}; CHECK(GetKeyboardState(original_keys));
    BYTE shortcut_keys[256]{}; shortcut_keys[VK_CONTROL]=0x80; shortcut_keys[VK_MENU]=0x80;
    CHECK(SetKeyboardState(shortcut_keys));
    PostMessageW(edit,WM_KEYDOWN,'D',0);
    MSG queued{};
    CHECK(GetMessageW(&queued,edit,WM_KEYDOWN,WM_KEYDOWN)>0);
    CHECK(SetKeyboardState(original_keys));
    CHECK(queued.message==WM_NULL); // thread hook consumes the key before the game's dispatch
    auto settings=FindWindowExW(window,nullptr,L"#32770",nullptr);
    CHECK(settings && settings!=window && GetDlgItem(settings,IDC_APPLY));
    check_covered(edit);
    CHECK(!IsWindowVisible(hidden));
    // A login/chat control may be raised, shown, or created during a frame.
    SetWindowPos(edit,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    CHECK(GetWindow(window,GW_CHILD)==settings);
    check_covered(edit);
    ShowWindow(edit,SW_HIDE); ShowWindow(edit,SW_SHOWNOACTIVATE);
    check_covered(edit);
    auto during=CreateWindowW(L"EDIT",L"during",WS_CHILD|WS_VISIBLE,10,10,20,12,window,nullptr,wc.hInstance,nullptr);
    CHECK(during); check_covered(during);
    auto group=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,30,20,window,nullptr,wc.hInstance,nullptr);
    CHECK(group);
    auto nested=CreateWindowW(L"EDIT",L"nested",WS_CHILD|WS_VISIBLE,1,1,15,10,group,nullptr,wc.hInstance,nullptr);
    CHECK(nested); check_covered(nested);
    CHECK(!(GetWindowLongPtrW(nested,GWL_STYLE)&WS_CLIPSIBLINGS));
    DestroyWindow(group); DestroyWindow(during);
    using ShowFn=int (WINAPI*)(BOOL);
    auto real_show=reinterpret_cast<ShowFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"ShowCursor"));
    CHECK(real_show);
    CHECK(GetCursor()==LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
    CHECK(ShowCursor(FALSE)==hidden_cursor_count-1); // game keeps hiding during overlay
    SetCursor(nullptr); CHECK(GetCursor()==LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
    int visible_count=real_show(TRUE)-1; real_show(FALSE); CHECK(visible_count>=0);
    SendMessageW(GetDlgItem(settings,IDC_APPLY),WM_SETCURSOR,0,MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));
    CHECK(GetCursor()==LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
    CHECK(GetParent(settings)==window && (GetWindowLongPtrW(settings,GWL_STYLE)&WS_CHILD));
    CHECK(!(GetWindowLongPtrW(settings,GWL_STYLE)&WS_CAPTION));
    RECT overlayrect{}, gameclient{}; GetClientRect(settings,&overlayrect); GetClientRect(window,&gameclient);
    CHECK(EqualRect(&overlayrect,&gameclient));
    CHECK(GetAsyncKeyState(VK_LBUTTON)==0 && GetKeyState(VK_CONTROL)==0);
    BYTE keys[256]{}; CHECK(GetKeyboardState(keys));
    for(auto key:keys) CHECK(key==0);
    // Painted choice buttons drive the same state as the settings model.
    SendMessageW(settings,WM_COMMAND,IDC_GDI,0);
    CHECK(SendDlgItemMessageW(settings,IDC_RENDERER,CB_GETCURSEL,0,0)==1);
    SendMessageW(settings,WM_COMMAND,IDC_GPU,0);
    CHECK(SendDlgItemMessageW(settings,IDC_RENDERER,CB_GETCURSEL,0,0)==0);
    SendMessageW(edit,WM_KEYDOWN,VK_F10,LPARAM(1)<<30);
    CHECK(FindWindowExW(window,nullptr,L"#32770",nullptr)==settings);
    CheckDlgButton(settings,IDC_SAVE,BST_UNCHECKED);
    SendDlgItemMessageW(settings,IDC_RENDERER,CB_SETCURSEL,1,0);
    SendMessageW(settings,WM_COMMAND,MAKEWPARAM(IDC_RENDERER,CBN_SELCHANGE),0);
    CHECK(!IsWindowEnabled(GetDlgItem(settings,IDC_BILINEAR)));
    CHECK(!IsWindowEnabled(GetDlgItem(settings,IDC_SHARP)));
    CHECK(IsWindowEnabled(GetDlgItem(settings,IDC_INTEGER)));
    SendMessageW(settings,WM_COMMAND,IDC_INTEGER,0);
    CHECK(SendDlgItemMessageW(settings,IDC_SCALING,CB_GETCURSEL,0,0)==hq::Integer);
    SendDlgItemMessageW(settings,IDC_MODE,CB_SETCURSEL,1,0);
    SendMessageW(settings,WM_COMMAND,IDC_APPLY,0);
    CHECK(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION));
    check_covered(edit);
    GetClientRect(settings,&overlayrect); GetClientRect(window,&gameclient); CHECK(EqualRect(&overlayrect,&gameclient));
    GetWindowTextW(edit,text,32); CHECK(wcscmp(text,L"HQNET")==0);
    SendDlgItemMessageW(settings,IDC_RENDERER,CB_SETCURSEL,0,0);
    SendDlgItemMessageW(settings,IDC_MODE,CB_SETCURSEL,0,0);
    SendMessageW(settings,WM_COMMAND,IDC_BILINEAR,0);
    SendMessageW(settings,WM_COMMAND,IDC_APPLY,0);
    CHECK(GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION);
    check_covered(edit);
    SendMessageW(settings,WM_CLOSE,0,0);
    CHECK(!IsWindow(settings) && GetFocus()==edit);
    CHECK(!(GetWindowLongPtrW(edit,GWL_STYLE)&WS_CLIPSIBLINGS));
    CHECK(!(GetWindowLongPtrW(hidden,GWL_STYLE)&WS_CLIPSIBLINGS));
    CHECK(GetWindowLongPtrW(clipped,GWL_STYLE)&WS_CLIPSIBLINGS);
    CHECK(IsWindowVisible(edit) && !IsWindowVisible(hidden));
    DWORD selection_start=0,selection_end=0;
    SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&selection_start),reinterpret_cast<LPARAM>(&selection_end));
    CHECK(selection_start==1 && selection_end==4);
    childdc=GetDC(edit); CHECK(childdc); RECT restored_clip{};
    CHECK(GetClipBox(childdc,&restored_clip)>NULLREGION); ReleaseDC(edit,childdc);
    GetWindowTextW(edit,text,32); CHECK(wcscmp(text,L"HQNET")==0);
    int restored_count=real_show(TRUE)-1; real_show(FALSE);
    CHECK(restored_count==hidden_cursor_count-1);
    CHECK(GetCursor()==nullptr);
    while(restored_count<initial_cursor_count) restored_count=real_show(TRUE);
    SetCursor(prior_cursor);
    // Preserve style changes made by the game between overlay sessions.
    SetWindowLongPtrW(hidden,GWL_STYLE,GetWindowLongPtrW(hidden,GWL_STYLE)|WS_CLIPSIBLINGS);
    SendMessageW(window,WM_SYSCOMMAND,0x1e30,0);
    settings=FindWindowExW(window,nullptr,L"#32770",nullptr);
    CHECK(settings!=window && SendDlgItemMessageW(settings,IDC_SCALING,CB_GETCURSEL,0,0)==hq::Bilinear);
    CHECK(SendDlgItemMessageW(settings,IDC_RENDERER,CB_GETCURSEL,0,0)==0);
    SendMessageW(settings,WM_COMMAND,IDC_SHARP,0); // close discards unapplied choices
    SendMessageW(settings,WM_CLOSE,0,0);
    CHECK(GetWindowLongPtrW(hidden,GWL_STYLE)&WS_CLIPSIBLINGS);
    SetWindowLongPtrW(hidden,GWL_STYLE,GetWindowLongPtrW(hidden,GWL_STYLE)&~LONG_PTR(WS_CLIPSIBLINGS));
    SendMessageW(window,WM_SYSCOMMAND,0x1e30,0);
    settings=FindWindowExW(window,nullptr,L"#32770",nullptr);
    CHECK(settings); check_covered(edit);
    CHECK(DestroyWindow(settings)); // external dialog destruction also restores styles
    CHECK(!(GetWindowLongPtrW(edit,GWL_STYLE)&WS_CLIPSIBLINGS));
    CHECK(!(GetWindowLongPtrW(hidden,GWL_STYLE)&WS_CLIPSIBLINGS));
    CHECK(GetWindowLongPtrW(clipped,GWL_STYLE)&WS_CLIPSIBLINGS);
    SendMessageW(window,WM_SYSCOMMAND,0x1e30,0);
    settings=FindWindowExW(window,nullptr,L"#32770",nullptr);
    CHECK(SendDlgItemMessageW(settings,IDC_SCALING,CB_GETCURSEL,0,0)==hq::Bilinear);
    if (argc==3) {
        // Only used with a disposable copy of the DLL/config, never a user's installation.
        CheckDlgButton(settings,IDC_SAVE,BST_CHECKED);
        SendMessageW(settings,WM_COMMAND,IDC_APPLY,0);
        wchar_t dllpath[32768]{}; GetModuleFileNameW(dll,dllpath,32768);
        std::wstring config(dllpath); config=config.substr(0,config.find_last_of(L"\\/")+1)+L"hqcdd.ini";
        CHECK(GetPrivateProfileIntW(L"Display",L"LinearFilter",-1,config.c_str())==1);
        wchar_t stored_filter[32]{};
        GetPrivateProfileStringW(L"Display",L"Scaling",L"",stored_filter,32,config.c_str());
        CHECK(wcscmp(stored_filter,L"bilinear")==0);
        CHECK(GetPrivateProfileIntW(L"Display",L"Fullscreen",-1,config.c_str())==0);
    }
    // Performance popup must not consume input or compete with the display dialog.
    SendMessageW(settings,WM_CLOSE,0,0);
    SetForegroundWindow(window);
    const auto osd_focus=GetFocus();
    SendMessageW(window,WM_SYSCOMMAND,0x1e40,0);
    const auto osd=FindWindowW(L"HQCDD.PerformanceOSD",L"HQCDD Performance");
    CHECK(osd && GetWindow(osd,GW_OWNER)==window);
    CHECK(GetFocus()==osd_focus);
    const auto osd_style=GetWindowLongPtrW(osd,GWL_EXSTYLE);
    CHECK((osd_style&(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW))==
        (WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW));
    CHECK(SendMessageW(osd,WM_NCHITTEST,0,0)==HTTRANSPARENT);
    CHECK(SendMessageW(osd,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE);
    SendMessageW(window,WM_SYSCOMMAND,0x1e50,0);
    SendMessageW(window,WM_SYSCOMMAND,0x1e50,0);
    SendMessageW(window,WM_SYSCOMMAND,0x1e40,0);
    CHECK(!IsWindowVisible(osd));
    SendMessageW(window,WM_SYSCOMMAND,0x1e40,0);
    SendMessageW(window,WM_SYSCOMMAND,0x1e30,0);
    settings=FindWindowExW(window,nullptr,L"#32770",nullptr);
    CHECK(settings && !IsWindowVisible(osd));
    // Leave both open to exercise owner/Draw teardown with live popups and hook.
    DestroyWindow(edit);
    clipper->Release();
    // Releasing Draw first must not invalidate surfaces or palettes.
    CHECK(d->Release()>0); back->Release(); front->Release(); CHECK(pal->Release()==0);
    CHECK(!(GetWindowLongPtrW(hidden,GWL_STYLE)&WS_CLIPSIBLINGS));
    CHECK(GetWindowLongPtrW(clipped,GWL_STYLE)&WS_CLIPSIBLINGS);
    CHECK(!IsWindowVisible(hidden) && !IsWindowVisible(clipped));
    DestroyWindow(window); UnregisterClassW(wc.lpszClassName,wc.hInstance);
    FreeLibrary(dll);
    CHECK(!IsWindow(settings));
    CHECK(!IsWindow(osd));
    std::puts("PASS: pixels, GPU/GDI, fullscreen, resize, child layout, settings live apply, focus, cancel and teardown");
    return 0;
}
