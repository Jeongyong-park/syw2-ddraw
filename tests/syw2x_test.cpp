#include "../src/syw2x.h"
#include "../src/palette_selection.h"
#include "../src/overlay.h"
#include "../src/settings_ids.h"
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
INT_PTR CALLBACK palette_dialog(HWND h,UINT msg,WPARAM w,LPARAM l) {
    auto overlay=reinterpret_cast<hq::Overlay*>(GetWindowLongPtrW(h,DWLP_USER));
    if(msg==WM_INITDIALOG) { SetWindowLongPtrW(h,DWLP_USER,l); return TRUE; }
    if(overlay && msg==WM_COMMAND) return overlay->palette_command(h,LOWORD(w),HIWORD(w));
    return FALSE;
}
void check_palette_ui() {
    hq::Overlay overlay; overlay.syw2x_available=true;
    overlay.aspect_available=true;
    overlay.aspect_wide=overlay.aspect_saved_wide=true; // Saved 16:9 can be pending/rejected.
    CHECK(!overlay.aspect_selection_changed()); // Unrelated temporary setting is allowed.
    overlay.aspect_wide=false;
    CHECK(overlay.aspect_selection_changed()); // Clearing the saved request needs persistence.
    overlay.aspect_saved_wide=false;
    CHECK(!overlay.aspect_selection_changed()); // Successful save resets the baseline.
    overlay.aspect_wide=true;
    CHECK(overlay.aspect_selection_changed());
    CHECK(overlay.palette[0x44]==0x04C804 && overlay.palette[0xFB]==0xBCBCC0);
    CHECK(overlay.palette[0x3D]==0x3C4474 && overlay.palette[0x21]==0xD08820);
    CHECK(overlay.palette[0xDB]==0x880C8C);
    overlay.palette[0x44]=0x123456; // Deliberately asymmetric R/B to detect COLORREF reversal.
    HWND owner=CreateWindowW(L"STATIC",L"palette test",WS_OVERLAPPED,0,0,817,639,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CHECK(owner);
    HWND panel=CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_DISPLAY),owner,palette_dialog,reinterpret_cast<LPARAM>(&overlay));
    CHECK(panel); overlay.aspect_available=true; overlay.init(panel);
    CHECK(IsWindowEnabled(GetDlgItem(panel,IDC_ASPECT_43)) && IsWindowEnabled(GetDlgItem(panel,IDC_ASPECT_169)));
    RECT mode{},aspect{},renderer{};
    GetWindowRect(GetDlgItem(panel,IDC_FULLSCREEN),&mode);
    GetWindowRect(GetDlgItem(panel,IDC_ASPECT_169),&aspect);
    GetWindowRect(GetDlgItem(panel,IDC_GDI),&renderer);
    CHECK(mode.bottom<=aspect.top && aspect.bottom<=renderer.top);
    overlay.notice_page=true; overlay.aspect_error=L"게임 실행 코드가 변경되어 16:9를 적용할 수 없습니다.";
    overlay.layout(panel);
    CHECK(GetWindowLongPtrW(GetDlgItem(panel,IDCANCEL),GWL_STYLE)&WS_VISIBLE);
    for(int id:{IDC_APPLY,IDC_TAB_DISPLAY,IDC_TAB_SYW2X,IDC_ASPECT_43,IDC_ASPECT_169,IDC_WINDOWED})
        CHECK(!(GetWindowLongPtrW(GetDlgItem(panel,id),GWL_STYLE)&WS_VISIBLE));
    wchar_t caption[32]{}; GetDlgItemTextW(panel,IDCANCEL,caption,32); CHECK(std::wstring(caption)==L"확인");
    overlay.notice_page=false; overlay.layout(panel);
    CHECK(GetWindowLongPtrW(GetDlgItem(panel,IDC_TAB_DISPLAY),GWL_STYLE)&WS_VISIBLE);
    overlay.syw2x_page=true;
    SetDlgItemTextW(panel,IDC_SYW2X_FIRST+5,L"0x44");
    SetDlgItemTextW(panel,IDC_SYW2X_FIRST+11,L"0xF4F3F2F1");
    overlay.layout(panel);
    CHECK(!(GetWindowLongPtrW(GetDlgItem(panel,IDC_ASPECT_169),GWL_STYLE)&WS_VISIBLE));
    auto swatch=GetDlgItem(panel,IDC_SWATCH_FIRST+1);
    CHECK(IsWindowEnabled(swatch));
    HDC dc=CreateCompatibleDC(nullptr); HBITMAP bitmap=CreateBitmap(40,30,1,32,nullptr);
    auto previous=SelectObject(dc,bitmap);
    DRAWITEMSTRUCT item{}; item.CtlID=IDC_SWATCH_FIRST+1; item.hwndItem=swatch; item.hDC=dc; item.rcItem={0,0,40,30};
    overlay.button(panel,item); CHECK(GetPixel(dc,20,15)==RGB(0x12,0x34,0x56));
    SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
    SendMessageW(panel,WM_COMMAND,IDC_SWATCH_FIRST+8,0);
    CHECK(overlay.palette_target==8 && overlay.palette_index==0xF3);
    auto grid=GetDlgItem(panel,IDC_PALETTE_GRID);
    MSG enter{}; enter.hwnd=grid; enter.message=WM_KEYDOWN; enter.wParam=VK_RETURN;
    CHECK(SendMessageW(grid,WM_GETDLGCODE,VK_RETURN,reinterpret_cast<LPARAM>(&enter))&DLGC_WANTMESSAGE);
    CHECK(overlay.palette_command(panel,IDC_APPLY,BN_CLICKED) && overlay.palette_target==8);
    SendMessageW(grid,WM_KEYDOWN,VK_UP,0); CHECK(overlay.palette_index==0xE3);
    SendMessageW(grid,WM_KEYDOWN,VK_RETURN,0);
    wchar_t input[64]{}; GetDlgItemTextW(panel,IDC_SYW2X_FIRST+11,input,64);
    CHECK(std::wstring(input)==L"0xF4E3F2F1" && overlay.palette_target==-1);
    SendMessageW(panel,WM_COMMAND,IDC_SWATCH_FIRST+1,0);
    SendMessageW(grid,WM_KEYDOWN,VK_END,0); CHECK(overlay.palette_index==255);
    SendMessageW(panel,WM_COMMAND,IDCANCEL,0);
    GetDlgItemTextW(panel,IDC_SYW2X_FIRST+5,input,64); CHECK(std::wstring(input)==L"0x44");
    SendMessageW(panel,WM_COMMAND,IDC_SWATCH_FIRST+1,0);
    RECT r{}; GetClientRect(grid,&r);
    SendMessageW(grid,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(r.right-1,r.bottom-1));
    CHECK(overlay.palette_index==255);
    SendMessageW(grid,WM_LBUTTONUP,0,MAKELPARAM(r.right-1,r.bottom-1));
    GetDlgItemTextW(panel,IDC_SYW2X_FIRST+5,input,64); CHECK(std::wstring(input)==L"0xFF");
    SetDlgItemTextW(panel,IDC_SYW2X_FIRST+5,L"invalid"); CHECK(!IsWindowEnabled(swatch));
    SendMessageW(panel,WM_COMMAND,IDC_SWATCH_FIRST+1,0); CHECK(overlay.palette_target==-1);
    SetDlgItemTextW(panel,IDC_SYW2X_FIRST+5,L"8"); overlay.syw2x_available=false; overlay.refresh_swatches(panel);
    CHECK(!IsWindowEnabled(swatch));
    SendMessageW(panel,WM_COMMAND,IDC_SWATCH_FIRST+1,0); CHECK(overlay.palette_target==-1);
    DestroyWindow(panel); DestroyWindow(owner);
}
int main() {
    wchar_t folder[MAX_PATH]{},temporary[MAX_PATH]{}; GetTempPathW(MAX_PATH,folder);
    if(!GetTempFileNameW(folder,L"sxT",0,temporary)) return 1;
    const std::filesystem::path path(temporary);
    try {
        check_palette_ui();
        uint32_t color=0;
        CHECK(hq::palette_value(L"08",0,color) && color==8);
        CHECK(!hq::palette_value(L"256",0,color));
        CHECK(!hq::palette_value(L"garbage",7,color));
        CHECK(hq::palette_value(L"0xF4F3F2F1",7,color));
        CHECK(hq::palette_byte(color,7)==0xF4 && hq::palette_byte(color,10)==0xF1);
        CHECK(hq::palette_replace(color,7,0x44)==0x44F3F2F1);
        CHECK(hq::palette_replace(color,8,0x44)==0xF444F2F1);
        CHECK(hq::palette_replace(color,9,0x44)==0xF4F344F1);
        CHECK(hq::palette_replace(color,10,0x44)==0xF4F3F244);
        CHECK(hq::palette_replace(0x44,0,255)==255);
        const std::string original="; keep comment\r\n[StatusBarStyle]\r\nHealthBarFillColor=0x44\r\nFutureKey=untouched\r\n[FutureSection]\r\nValue=123\r\n";
        { std::ofstream f(path,std::ios::binary); f<<original; }
        hq::Syw2xConfig config; CHECK(config.load(path.wstring()));
        auto next=config.values; std::wstring status;
        CHECK(config.save(next,status));
        std::vector<char> bytes; bool exists=false;
        CHECK(hq::Syw2xConfig::read(path.wstring(),bytes,exists)); CHECK(std::string(bytes.begin(),bytes.end())==original);
        next[5]=L"256"; CHECK(!config.save(next,status));
        next[5]=L"0x55"; next[0]=L"1"; CHECK(config.save(next,status));
        CHECK(config.values[0]==L"1" && config.values[5]==L"0x55");
        bytes.clear(); CHECK(hq::Syw2xConfig::read(path.wstring(),bytes,exists));
        const std::string saved(bytes.begin(),bytes.end());
        CHECK(saved.find("; keep comment")!=std::string::npos && saved.find("FutureKey=untouched")!=std::string::npos && saved.find("Value=123")!=std::string::npos);
        { std::ofstream f(path,std::ios::binary|std::ios::app); f<<"; external edit\r\n"; }
        next=config.values; next[5]=L"0x66"; CHECK(!config.save(next,status));
        CHECK(config.load(path.wstring())); next=config.values; next[11]=L"0xFFFFFFFF"; CHECK(config.save(next,status));
        CHECK(SetFileAttributesW(temporary,FILE_ATTRIBUTE_READONLY));
        next=config.values; next[5]=L"0x77"; CHECK(!config.save(next,status));
        CHECK(SetFileAttributesW(temporary,FILE_ATTRIBUTE_NORMAL));
        CHECK(config.load(path.wstring()) && config.values[5]==L"0x55");
        for(const auto* bad:{L"-1",L"+1",L"12oops",L"",L"0x100000000",L"99999999999999999999999"}) CHECK(!hq::Syw2xConfig::valid(bad,0xffffffffULL));
        CHECK(hq::Syw2xConfig::valid(L"255",255)); CHECK(hq::Syw2xConfig::valid(L"0xFF",255));
        CHECK(hq::Syw2xConfig::valid(L"08",255));
        DeleteFileW(temporary); CHECK(config.load(path.wstring())); CHECK(!config.existed);
        CHECK(config.save(config.values,status)); CHECK(config.existed);
        DeleteFileW(temporary); puts("PASS: SYW2X defaults, ranges, atomic save, unknown fields and external-edit protection");
    } catch(const std::exception& e) { SetFileAttributesW(temporary,FILE_ATTRIBUTE_NORMAL); DeleteFileW(temporary); fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
