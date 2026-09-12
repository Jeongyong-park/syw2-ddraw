#include <windows.h>
#include <ddraw.h>
#include "../src/settings_ids.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int clicks=0;
HWND game_window=nullptr;
LRESULT CALLBACK game_proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_LBUTTONDOWN) ++clicks;
    return DefWindowProcW(h,msg,w,l);
}
void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
        auto panel=game_window?FindWindowExW(game_window,nullptr,L"#32770",nullptr):nullptr;
        if(!panel || !IsDialogMessageW(panel,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
int wmain(int argc,wchar_t** argv) {
    try {
        CHECK(argc==2 || (argc==3 && wcscmp(argv[2],L"--escape")==0));
        wchar_t self[32768]{}; CHECK(GetModuleFileNameW(nullptr,self,32768));
        const auto folder=std::filesystem::path(self).parent_path()/L"aspect-notice-fixture";
        std::filesystem::create_directories(folder);
        const auto plugin=folder/L"hqcdd.asi";
        std::filesystem::copy_file(argv[1],plugin,std::filesystem::copy_options::overwrite_existing);
        const std::string config="[Display]\nBattleAspect=16:9\nFullscreen=0\nRenderer=gdi\n";
        { std::ofstream ini(folder/L"hqcdd.ini"); ini<<config; }
        auto dll=LoadLibraryW(plugin.c_str()); CHECK(dll);
        using Create=HRESULT(WINAPI*)(GUID*,void**,REFIID,IUnknown*);
        auto create=reinterpret_cast<Create>(GetProcAddress(dll,"DirectDrawCreateEx")); CHECK(create);
        WNDCLASSW wc{}; wc.lpfnWndProc=game_proc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"HQCDD_NOTICE_TEST";
        CHECK(RegisterClassW(&wc));
        auto window=CreateWindowW(wc.lpszClassName,L"Aspect failure notice test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            100,100,800,600,nullptr,nullptr,wc.hInstance,nullptr); CHECK(window);
        game_window=window;
        IDirectDraw7* draw=nullptr;
        CHECK(create(nullptr,reinterpret_cast<void**>(&draw),IID_IDirectDraw7,nullptr)==DD_OK);
        CHECK(draw->SetCooperativeLevel(window,DDSCL_NORMAL)==DD_OK);
        CHECK(draw->SetDisplayMode(800,600,8,0,0)==DD_OK);
        auto edit=CreateWindowW(L"EDIT",L"keep input",WS_CHILD|WS_VISIBLE,20,20,200,30,window,nullptr,wc.hInstance,nullptr);
        CHECK(edit); ShowWindow(window,SW_SHOW); SetActiveWindow(window); SetForegroundWindow(window); SetFocus(edit); pump();
        // An interactive runner can inherit a background shell. Temporarily join
        // the foreground input queue to activate only this test's own window.
        if(GetForegroundWindow()!=window) {
            const DWORD foreground=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
            const DWORD current=GetCurrentThreadId();
            const bool attached=foreground!=current && AttachThreadInput(current,foreground,TRUE);
            SetForegroundWindow(window); SetFocus(edit);
            if(attached) AttachThreadInput(current,foreground,FALSE);
            pump();
        }
        CHECK(GetForegroundWindow()==window);
        DDSURFACEDESC2 desc{}; desc.dwSize=sizeof(desc); desc.dwFlags=DDSD_CAPS; desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;
        IDirectDrawSurface7* surface=nullptr; CHECK(draw->CreateSurface(&desc,&surface,nullptr)==DD_OK);
        DDBLTFX fill{}; fill.dwSize=sizeof(fill);
        CHECK(surface->Blt(nullptr,nullptr,nullptr,DDBLT_COLORFILL,&fill)==DD_OK);
        pump();
        HWND panel=nullptr;
        for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
            if(GetDlgItem(child,IDC_STATUS)) panel=child;
        CHECK(panel && IsWindowVisible(panel));
        wchar_t status[256]{},caption[32]{};
        GetDlgItemTextW(panel,IDC_STATUS,status,256); CHECK(*status);
        GetDlgItemTextW(panel,IDCANCEL,caption,32); CHECK(wcscmp(caption,L"확인")==0);
        CHECK(GetFocus()==GetDlgItem(panel,IDCANCEL));
        CHECK(!IsWindowVisible(GetDlgItem(panel,IDC_APPLY)));
        if(argc==3) PostMessageW(GetFocus(),WM_KEYDOWN,VK_ESCAPE,0);
        else SendMessageW(panel,WM_COMMAND,IDCANCEL,0);
        pump();
        CHECK(!IsWindow(panel) && GetFocus()==edit);
        wchar_t input[32]{}; GetWindowTextW(edit,input,32); CHECK(wcscmp(input,L"keep input")==0);
        SendMessageW(window,WM_LBUTTONDOWN,0,MAKELPARAM(300,300)); CHECK(clicks==1);
        for(int i=0;i<3;++i) { CHECK(surface->Blt(nullptr,nullptr,nullptr,DDBLT_COLORFILL,&fill)==DD_OK); pump(); }
        for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) CHECK(!GetDlgItem(child,IDC_STATUS));
        std::ifstream ini(folder/L"hqcdd.ini"); const std::string saved((std::istreambuf_iterator<char>(ini)),{});
        CHECK(saved==config);
        surface->Release(); draw->Release(); DestroyWindow(window); FreeLibrary(dll);
        std::puts("PASS: failed 16:9 request shows one overlay, preserves INI, restores focus/text/clicks");
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
