#include "version.h"
// SYW2Plus-specific DirectDraw 7 software presentation layer.
// No process-wide palette, display-mode switch, driver, injection, or network hook.
#include <windows.h>
#include <commctrl.h>
#include <ddraw.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <map>
#include <windowsx.h>
#include "pixels.h"
#include "viewport.h"
#include "gpu.h"
#include "settings_ids.h"
#include "overlay.h"

namespace {
std::recursive_mutex mutex;
using Guard = std::lock_guard<std::recursive_mutex>;
HMODULE module = nullptr;
FARPROC system_ddraw_proc(const char* name) {
    // Keep the system module loaded while delegated COM objects can outlive a call.
    static HMODULE system=[]() -> HMODULE {
        wchar_t path[MAX_PATH]{};
        const UINT length=GetSystemDirectoryW(path,MAX_PATH);
        if(!length || length>=MAX_PATH || wcscat_s(path,L"\\ddraw.dll")!=0) return nullptr;
        return LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    return system ? GetProcAddress(system,name) : nullptr;
}
std::wstring local_path(const wchar_t* name) {
    wchar_t path[32768]{};
    GetModuleFileNameW(module, path, 32768);
    std::wstring s(path);
    return s.substr(0, s.find_last_of(L"\\/") + 1) + name;
}
void log(const char* format, ...) {
    Guard lock(mutex);
    char line[1024];
    va_list args; va_start(args, format);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args); va_end(args);
    OutputDebugStringA(line); OutputDebugStringA("\n");
    FILE* f = nullptr;
    if (_wfopen_s(&f, local_path(L"hqcdd.log").c_str(), L"ab") == 0) {
        fprintf(f, "[%lu] %s\n", GetCurrentProcessId(), line); fclose(f);
    }
}
HRESULT unsupported(const char* name) {
    Guard lock(mutex);
    static std::set<std::string> seen;
    if (seen.insert(name).second) log("unsupported: %s", name);
    return DDERR_UNSUPPORTED;
}
#define UNSUP(name, signature) HRESULT STDMETHODCALLTYPE name signature override { return unsupported(#name); }
#define REFCOUNT() \
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; } \
    ULONG STDMETHODCALLTYPE Release() override { Guard lock(mutex); auto n = --refs; if (!n) delete this; return n; }

class Draw;
class Surface;
class Palette;
Draw* mouse_draw = nullptr;
void** mouse_slot = nullptr;
using CursorFn = BOOL (WINAPI*)(LPPOINT);
CursorFn original_cursor = ::GetCursorPos;
BOOL WINAPI game_cursor(LPPOINT point);
bool hook_cursor(Draw* draw);
LRESULT CALLBACK window_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK child_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
constexpr UINT WM_HQ_LAYOUT=WM_APP+0x6d0;
constexpr UINT MENU_FULLSCREEN=0x1e10, MENU_WINDOWED=0x1e20, MENU_SETTINGS=0x1e30;
INT_PTR CALLBACK settings_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK settings_keys(int,WPARAM,LPARAM);
HWND settings_window=nullptr;
Draw* shortcut_draw=nullptr;
void hook_overlay_input();
void restore_overlay_input();
void begin_overlay_input();
void begin_overlay_cursor();
void end_overlay_cursor();
constexpr DWORD WINDOW_STYLE=WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN;
struct Child {
    RECT logical{};
    HFONT original_font=nullptr, scaled_font=nullptr;
    LOGFONTW font{};
    int font_height=0, font_width=0;
    bool original_clip_siblings=false;
    bool overlay_clipping=false;
};
std::set<Surface*> surfaces;
std::set<Palette*> palettes;
DDPIXELFORMAT pixel_format(int bpp) {
    DDPIXELFORMAT p{}; p.dwSize = sizeof(p); p.dwFlags = DDPF_RGB;
    p.dwRGBBitCount = bpp;
    if (bpp == 8) p.dwFlags |= DDPF_PALETTEINDEXED8;
    else if (bpp == 16) { p.dwRBitMask = 0xf800; p.dwGBitMask = 0x7e0; p.dwBBitMask = 0x1f; }
    else { p.dwRBitMask = 0xff0000; p.dwGBitMask = 0xff00; p.dwBBitMask = 0xff; }
    return p;
}
hq::Rect rect(LPRECT r, int w, int h) { return r ? hq::Rect{r->left,r->top,r->right,r->bottom} : hq::Rect{0,0,w,h}; }

class Draw final : public IDirectDraw7 {
public:
    std::atomic<ULONG> refs{1};
    HWND window = nullptr;
    int width = 800, height = 600, bpp = 8;
    Surface* primary = nullptr; // weak; surfaces keep Draw alive
    bool windowed = true;
    LONG_PTR old_style = 0, old_exstyle = 0;
    RECT old_rect{};
    bool styled = false;
    bool layout_busy=false, clip_owned=false, eat_enter=false;
    WINDOWPLACEMENT windowed_placement{sizeof(WINDOWPLACEMENT)};
    bool have_placement=false;
    std::map<HWND,Child> children;
    std::unique_ptr<hq::Gpu> gpu;
    bool gpu_enabled=true, gpu_reported=false, presenting=false, vsync=false;
    int scaling=hq::SharpBilinear;
    HWND settings=nullptr, previous_focus=nullptr;
    HHOOK settings_hook=nullptr;
    bool gpu_preferred=true;
    hq::Overlay overlay;
    bool opening_settings=false;
    Draw() {
        windowed=GetPrivateProfileIntW(L"Display",L"Fullscreen",0,local_path(L"hqcdd.ini").c_str())==0;
        wchar_t renderer[32]{};
        GetPrivateProfileStringW(L"Display",L"Renderer",L"auto",renderer,32,local_path(L"hqcdd.ini").c_str());
        gpu_enabled=_wcsicmp(renderer,L"gdi")!=0;
        gpu_preferred=gpu_enabled;
        vsync=GetPrivateProfileIntW(L"Display",L"VSync",0,local_path(L"hqcdd.ini").c_str())!=0;
        const bool legacy_linear=GetPrivateProfileIntW(L"Display",L"LinearFilter",0,local_path(L"hqcdd.ini").c_str())!=0;
        wchar_t legacy_filter[32]{};
        GetPrivateProfileStringW(L"Display",L"LinearFilter",L"",legacy_filter,32,local_path(L"hqcdd.ini").c_str());
        wchar_t filter[32]{};
        GetPrivateProfileStringW(L"Display",L"Scaling",L"",filter,32,local_path(L"hqcdd.ini").c_str());
        scaling=hq::parse_scaling(filter,legacy_linear,legacy_filter[0]!=L'\0');
        log("HQCDD " HQCDD_VERSION " created; renderer=%ls",renderer);
    }
    ~Draw();
    REFCOUNT()
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDirectDraw7) return E_NOINTERFACE;
        *out = this; AddRef(); return DD_OK;
    }
    void resize();
    void present();
    hq::Viewport viewport() const;
    void set_windowed(bool value);
    void sync_children();
    void add_child(HWND child);
    void remove_child(HWND child, bool restore);
    void fit_child(HWND child);
    void update_child_clipping(HWND child);
    void update_children_clipping();
    RECT child_rect(HWND child, RECT logical) const;
    void update_clip();
    bool hotkey(UINT msg, WPARAM w, LPARAM l);
    void open_settings();
    void close_settings();
    void apply_settings();
    HRESULT STDMETHODCALLTYPE Compact() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE CreateClipper(DWORD flags, IDirectDrawClipper** out, IUnknown* outer) override;
    HRESULT STDMETHODCALLTYPE CreatePalette(DWORD flags, PALETTEENTRY* entries, IDirectDrawPalette** out, IUnknown* outer) override;
    HRESULT STDMETHODCALLTYPE CreateSurface(DDSURFACEDESC2* desc, IDirectDrawSurface7** out, IUnknown* outer) override;
    UNSUP(DuplicateSurface, (IDirectDrawSurface7*, IDirectDrawSurface7**))
    HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD, DDSURFACEDESC2* filter, void* ctx, LPDDENUMMODESCALLBACK2 callback) override;
    UNSUP(EnumSurfaces, (DWORD, DDSURFACEDESC2*, void*, LPDDENUMSURFACESCALLBACK7))
    HRESULT STDMETHODCALLTYPE FlipToGDISurface() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetCaps(DDCAPS* driver, DDCAPS* hel) override;
    HRESULT STDMETHODCALLTYPE GetDisplayMode(DDSURFACEDESC2* out) override;
    HRESULT STDMETHODCALLTYPE GetFourCCCodes(DWORD* count, DWORD*) override { if (!count) return E_POINTER; *count=0; return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetGDISurface(IDirectDrawSurface7** out) override;
    HRESULT STDMETHODCALLTYPE GetMonitorFrequency(DWORD* out) override { if (!out) return E_POINTER; *out=60; return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetScanLine(DWORD* out) override { if (!out) return E_POINTER; *out=0; return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(BOOL* out) override { if (!out) return E_POINTER; *out=TRUE; return DD_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(GUID*) override { return DDERR_ALREADYINITIALIZED; }
    HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND h, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD w, DWORD h, DWORD bits, DWORD, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD flags, HANDLE event) override {
        if (event || flags == DDWAITVB_BLOCKBEGINEVENT) return DDERR_UNSUPPORTED;
        if (flags != DDWAITVB_BLOCKBEGIN && flags != DDWAITVB_BLOCKEND) return DDERR_INVALIDPARAMS;
        Sleep(1); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAvailableVidMem(DDSCAPS2*, DWORD* total, DWORD* free) override {
        if (total) *total=128*1024*1024; if (free) *free=128*1024*1024; return DD_OK;
    }
    UNSUP(GetSurfaceFromDC, (HDC, IDirectDrawSurface7**))
    HRESULT STDMETHODCALLTYPE RestoreAllSurfaces() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceIdentifier(DDDEVICEIDENTIFIER2* out, DWORD) override {
        if (!out) return E_POINTER; *out={};
        strcpy_s(out->szDriver,"hqcdd.dll"); strcpy_s(out->szDescription,"HQNET SYW2Plus software DirectDraw"); return DD_OK;
    }
    UNSUP(StartModeTest, (SIZE*, DWORD, DWORD))
    UNSUP(EvaluateMode, (DWORD, DWORD*))
};

class Palette final : public IDirectDrawPalette {
public:
    std::atomic<ULONG> refs{1};
    Draw* draw;
    DWORD caps;
    PALETTEENTRY entries[256]{};
    Palette(Draw* d, DWORD c, PALETTEENTRY* e) : draw(d), caps(c) {
        std::memcpy(entries,e,sizeof(entries)); palettes.insert(this); draw->AddRef();
    }
    ~Palette() { palettes.erase(this); draw->Release(); }
    REFCOUNT()
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (id != IID_IUnknown && id != IID_IDirectDrawPalette) return E_NOINTERFACE;
        *out=this; AddRef(); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCaps(DWORD* out) override { if (!out) return E_POINTER; *out=caps; return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetEntries(DWORD flags, DWORD base, DWORD count, PALETTEENTRY* out) override {
        Guard lock(mutex);
        if (flags || !out || base>256 || count>256-base) return DDERR_INVALIDPARAMS;
        std::memcpy(out,entries+base,count*sizeof(PALETTEENTRY)); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IDirectDraw*, DWORD, PALETTEENTRY*) override { return DDERR_ALREADYINITIALIZED; }
    HRESULT STDMETHODCALLTYPE SetEntries(DWORD flags, DWORD base, DWORD count, PALETTEENTRY* in) override;
    hq::Palette colors() const {
        hq::Palette out{};
        for (int i=0;i<256;++i) out[i]=(uint32_t(entries[i].peRed)<<16)|(uint32_t(entries[i].peGreen)<<8)|entries[i].peBlue;
        return out;
    }
};

class Surface final : public IDirectDrawSurface7 {
public:
    std::atomic<ULONG> refs{1};
    Draw* draw;
    std::unique_ptr<hq::Image> image;
    DWORD caps;
    Surface* back = nullptr;
    Palette* palette = nullptr;
    IDirectDrawClipper* clipper = nullptr;
    bool locked = false;
    DWORD lock_flags = 0;
    bool has_src_key=false, has_dst_key=false;
    DDCOLORKEY src_key{},dst_key{};
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ old_bitmap=nullptr;
    void* dib_bits=nullptr;
    Surface(Draw* d, int w, int h, int bits, DWORD c) : draw(d), image(std::make_unique<hq::Image>(w,h,bits)), caps(c) {
        surfaces.insert(this); draw->AddRef();
    }
    ~Surface() {
        if (draw->primary == this) draw->primary=nullptr;
        if (dc) { SelectObject(dc,old_bitmap); DeleteDC(dc); DeleteObject(bitmap); }
        if (back) back->Release(); if (palette) palette->Release(); if (clipper) clipper->Release();
        surfaces.erase(this); draw->Release();
    }
    REFCOUNT()
    bool primary() const { return (caps & DDSCAPS_PRIMARYSURFACE)!=0; }
    bool busy() const { return locked || dc; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (id != IID_IUnknown && id != IID_IDirectDrawSurface7) return E_NOINTERFACE;
        *out=this; AddRef(); return DD_OK;
    }
    void describe(DDSURFACEDESC2* out) {
        *out={}; out->dwSize=sizeof(*out);
        out->dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH|DDSD_PIXELFORMAT;
        out->dwWidth=image->width; out->dwHeight=image->height; out->lPitch=image->pitch;
        out->ddpfPixelFormat=pixel_format(image->bpp); out->ddsCaps.dwCaps=caps;
        if (back) { out->dwFlags|=DDSD_BACKBUFFERCOUNT; out->dwBackBufferCount=1; }
    }
    UNSUP(AddAttachedSurface, (IDirectDrawSurface7*))
    UNSUP(AddOverlayDirtyRect, (RECT*))
    HRESULT STDMETHODCALLTYPE Blt(RECT* dest, IDirectDrawSurface7* source, RECT* src, DWORD flags, DDBLTFX* fx) override;
    UNSUP(BltBatch, (DDBLTBATCH*, DWORD, DWORD))
    HRESULT STDMETHODCALLTYPE BltFast(DWORD x, DWORD y, IDirectDrawSurface7* source, RECT* src, DWORD flags) override;
    UNSUP(DeleteAttachedSurface, (DWORD, IDirectDrawSurface7*))
    HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(void* ctx, LPDDENUMSURFACESCALLBACK7 cb) override {
        Guard lock(mutex); if (!cb) return DDERR_INVALIDPARAMS;
        if (back) { DDSURFACEDESC2 d{}; back->describe(&d); back->AddRef(); cb(back,&d,ctx); }
        return DD_OK;
    }
    UNSUP(EnumOverlayZOrders, (DWORD, void*, LPDDENUMSURFACESCALLBACK7))
    HRESULT STDMETHODCALLTYPE Flip(IDirectDrawSurface7* target, DWORD flags) override {
        Guard lock(mutex);
        if (flags & ~(DDFLIP_WAIT|DDFLIP_DONOTWAIT|DDFLIP_NOVSYNC)) return DDERR_UNSUPPORTED;
        if (!back || (target && target!=back)) return DDERR_NOTFLIPPABLE;
        if (busy() || back->busy()) return DDERR_SURFACEBUSY;
        image.swap(back->image); draw->present(); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAttachedSurface(DDSCAPS2* c, IDirectDrawSurface7** out) override {
        Guard lock(mutex); if (!out) return E_POINTER; *out=nullptr;
        if (!c) return DDERR_INVALIDPARAMS;
        if (!back || !(c->dwCaps & DDSCAPS_BACKBUFFER)) return DDERR_NOTFOUND;
        *out=back; back->AddRef(); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD) override { return busy()?DDERR_WASSTILLDRAWING:DD_OK; }
    HRESULT STDMETHODCALLTYPE GetCaps(DDSCAPS2* out) override { if (!out) return E_POINTER; *out={}; out->dwCaps=caps; return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetClipper(IDirectDrawClipper** out) override {
        Guard lock(mutex); if (!out) return E_POINTER; *out=clipper;
        if (!clipper) return DDERR_NOCLIPPERATTACHED; clipper->AddRef(); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE GetColorKey(DWORD flags, DDCOLORKEY* out) override {
        if (!out) return E_POINTER;
        if (flags==DDCKEY_SRCBLT && has_src_key) { *out=src_key; return DD_OK; }
        if (flags==DDCKEY_DESTBLT && has_dst_key) { *out=dst_key; return DD_OK; }
        return DDERR_NOCOLORKEY;
    }
    HRESULT STDMETHODCALLTYPE GetDC(HDC* out) override;
    HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD) override { return busy()?DDERR_WASSTILLDRAWING:DD_OK; }
    UNSUP(GetOverlayPosition, (LONG*, LONG*))
    HRESULT STDMETHODCALLTYPE GetPalette(IDirectDrawPalette** out) override {
        Guard lock(mutex); if (!out) return E_POINTER; *out=palette;
        if (!palette) return DDERR_NOPALETTEATTACHED; palette->AddRef(); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(DDPIXELFORMAT* out) override {
        if (!out || out->dwSize!=sizeof(*out)) return DDERR_INVALIDPARAMS;
        *out=pixel_format(image->bpp); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSurfaceDesc(DDSURFACEDESC2* out) override {
        Guard lock(mutex); if (!out || out->dwSize!=sizeof(*out)) return DDERR_INVALIDPARAMS; describe(out); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IDirectDraw*, DDSURFACEDESC2*) override { return DDERR_ALREADYINITIALIZED; }
    HRESULT STDMETHODCALLTYPE IsLost() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE Lock(RECT* area, DDSURFACEDESC2* out, DWORD flags, HANDLE event) override {
        Guard lock(mutex);
        if (!out || out->dwSize!=sizeof(*out) || event) return DDERR_INVALIDPARAMS;
        const auto r=rect(area,image->width,image->height);
        if (!hq::valid_rect(r) || r.left<0 || r.top<0 || r.right>image->width || r.bottom>image->height) return DDERR_INVALIDRECT;
        if (busy()) return DDERR_SURFACEBUSY;
        describe(out); out->dwFlags|=DDSD_LPSURFACE;
        out->dwWidth=r.right-r.left; out->dwHeight=r.bottom-r.top;
        out->lpSurface=image->bytes.data()+size_t(r.top)*image->pitch+r.left*(image->bpp/8);
        locked=true; lock_flags=flags; return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC h) override {
        Guard lock(mutex); if (!dc || h!=dc) return DDERR_INVALIDPARAMS;
        GdiFlush(); std::memcpy(image->bytes.data(),dib_bits,image->bytes.size());
        SelectObject(dc,old_bitmap); DeleteDC(dc); DeleteObject(bitmap);
        dc=nullptr; bitmap=nullptr; dib_bits=nullptr; old_bitmap=nullptr;
        if (primary()) draw->present(); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE Restore() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE SetClipper(IDirectDrawClipper* c) override {
        Guard lock(mutex); if (c) c->AddRef(); if (clipper) clipper->Release(); clipper=c; return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE SetColorKey(DWORD flags, DDCOLORKEY* key) override {
        Guard lock(mutex); flags &= ~DDCKEY_COLORSPACE;
        if (flags==DDCKEY_SRCBLT) { has_src_key=key!=nullptr; if(key) src_key=*key; return DD_OK; }
        if (flags==DDCKEY_DESTBLT) { has_dst_key=key!=nullptr; if(key) dst_key=*key; return DD_OK; }
        return DDERR_UNSUPPORTED;
    }
    UNSUP(SetOverlayPosition, (LONG, LONG))
    HRESULT STDMETHODCALLTYPE SetPalette(IDirectDrawPalette* p) override {
        Guard lock(mutex); auto v=static_cast<Palette*>(p);
        if (p && (!palettes.count(v) || v->draw!=draw || image->bpp!=8)) return DDERR_INVALIDPARAMS;
        if (v) v->AddRef(); if (palette) palette->Release(); palette=v;
        if (primary()) draw->present(); return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock(RECT*) override {
        Guard lock(mutex); if (!locked) return DDERR_NOTLOCKED;
        locked=false; if (primary() && !(lock_flags & DDLOCK_READONLY)) draw->present(); return DD_OK;
    }
    UNSUP(UpdateOverlay, (RECT*, IDirectDrawSurface7*, RECT*, DWORD, DDOVERLAYFX*))
    UNSUP(UpdateOverlayDisplay, (DWORD))
    UNSUP(UpdateOverlayZOrder, (DWORD, IDirectDrawSurface7*))
    HRESULT STDMETHODCALLTYPE GetDDInterface(void** out) override { if (!out) return E_POINTER; *out=draw; draw->AddRef(); return DD_OK; }
    HRESULT STDMETHODCALLTYPE PageLock(DWORD) override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE PageUnlock(DWORD) override { return DD_OK; }
    UNSUP(SetSurfaceDesc, (DDSURFACEDESC2*, DWORD))
    UNSUP(SetPrivateData, (REFGUID, void*, DWORD, DWORD))
    UNSUP(GetPrivateData, (REFGUID, void*, DWORD*))
    UNSUP(FreePrivateData, (REFGUID))
    UNSUP(GetUniquenessValue, (DWORD*))
    UNSUP(ChangeUniquenessValue, ())
    UNSUP(SetPriority, (DWORD))
    UNSUP(GetPriority, (DWORD*))
    UNSUP(SetLOD, (DWORD))
    UNSUP(GetLOD, (DWORD*))
};

LRESULT CALLBACK window_proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto d=reinterpret_cast<Draw*>(data);
    Guard lock(mutex);
    if (d->hotkey(msg,w,l)) return 0;
    if (msg==WM_INITMENU) {
        auto result=DefSubclassProc(h,msg,w,l);
        auto menu=GetSystemMenu(h,FALSE);
        if (menu && reinterpret_cast<HMENU>(w)==menu && GetMenuState(menu,MENU_SETTINGS,MF_BYCOMMAND)==UINT(-1)) {
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,MENU_SETTINGS,L"디스플레이 설정 (Ctrl+Alt+D)");
            AppendMenuW(menu,MF_STRING,MENU_FULLSCREEN,L"전체화면 (Alt+Enter)");
            AppendMenuW(menu,MF_STRING,MENU_WINDOWED,L"창 모드 (Alt+Enter)");
        }
        return result;
    }
    if (msg==WM_SYSCOMMAND) {
        if ((w&0xfff0)==MENU_SETTINGS) { d->open_settings(); return 0; }
        if ((w&0xfff0)==MENU_FULLSCREEN) { d->set_windowed(false); return 0; }
        if ((w&0xfff0)==MENU_WINDOWED) { d->set_windowed(true); return 0; }
    }
    if (msg==WM_PARENTNOTIFY && LOWORD(w)==WM_CREATE) d->add_child(reinterpret_cast<HWND>(l));
    if (msg==WM_HQ_LAYOUT) { d->sync_children(); d->present(); return 0; }
    if (msg==WM_SIZE || msg==WM_MOVE) {
        if(msg==WM_SIZE && d->settings && !d->layout_busy) d->overlay.layout(d->settings);
        // Do not expose the physical presentation size to legacy game logic.
        // DirectDraw's logical mode remains unchanged across Alt+Enter/resize.
        if (!d->layout_busy && w!=SIZE_MINIMIZED) { d->sync_children(); d->present(); }
        d->update_clip();
        if (msg==WM_SIZE && w==SIZE_MAXIMIZED && d->windowed && !d->layout_busy)
            d->have_placement=GetWindowPlacement(h,&d->windowed_placement)!=FALSE;
        if (msg==WM_SIZE) return 0;
    }
    if (msg==WM_EXITSIZEMOVE && d->windowed)
        d->have_placement=GetWindowPlacement(h,&d->windowed_placement)!=FALSE;
    if (msg==WM_GETMINMAXINFO && d->windowed) {
        auto info=reinterpret_cast<MINMAXINFO*>(l);
        info->ptMinTrackSize={160,120}; return 0;
    }
    if (msg==WM_DISPLAYCHANGE || msg==WM_DPICHANGED) {
        if (!d->windowed) d->resize();
        else if (msg==WM_DPICHANGED) {
            const auto r=reinterpret_cast<RECT*>(l);
            SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        d->sync_children(); d->update_clip(); d->present(); return 0;
    }
    if (msg==WM_ACTIVATEAPP) {
        if (!w && d->clip_owned) { ClipCursor(nullptr); d->clip_owned=false; }
        if (w) { d->update_clip(); InvalidateRect(h,nullptr,FALSE); }
    }
    if (msg==WM_ERASEBKGND) return 1; // present paints both the image and letterbox bars
    if (msg==WM_PAINT) {
        PAINTSTRUCT ps{}; BeginPaint(h,&ps); EndPaint(h,&ps);
        d->present(); return 0;
    }
    if (msg==WM_NCDESTROY) {
        d->close_settings();
        if (d->settings_hook) { UnhookWindowsHookEx(d->settings_hook); d->settings_hook=nullptr; }
        if (shortcut_draw==d) shortcut_draw=nullptr;
        if (d->clip_owned) { ClipCursor(nullptr); d->clip_owned=false; }
        RemoveWindowSubclass(h,window_proc,1); d->window=nullptr; d->styled=false;
    }
    if (msg>=WM_MOUSEMOVE && msg<=WM_MBUTTONDBLCLK) {
        auto v=d->viewport();
        int x=std::clamp(v.unmap_x(GET_X_LPARAM(l)),0,d->width-1);
        int y=std::clamp(v.unmap_y(GET_Y_LPARAM(l)),0,d->height-1);
        l=MAKELPARAM(x,y);
    }
    return DefSubclassProc(h,msg,w,l);
}

LRESULT CALLBACK child_proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto d=reinterpret_cast<Draw*>(data);
    Guard lock(mutex);
    if (d->hotkey(msg,w,l)) return 0; // Alt+Enter also works with the ID/IME control focused
    if (msg==WM_PARENTNOTIFY && LOWORD(w)==WM_CREATE) d->add_child(reinterpret_cast<HWND>(l));
    auto it=d->children.find(h);
    if (msg==WM_WINDOWPOSCHANGING && d->settings && GetParent(h)==d->window) {
        auto pos=reinterpret_cast<WINDOWPOS*>(l);
        // Game controls may be raised or created while the overlay is open.
        if (!(pos->flags&SWP_NOZORDER) &&
            (pos->hwndInsertAfter==HWND_TOP || pos->hwndInsertAfter==HWND_TOPMOST || pos->hwndInsertAfter==HWND_NOTOPMOST))
            pos->hwndInsertAfter=d->settings;
    }
    if (it!=d->children.end() && !d->layout_busy) {
        auto& child=it->second;
        if (msg==WM_WINDOWPOSCHANGING) {
            // Calls from the game still express child positions in game pixels.
            auto pos=reinterpret_cast<WINDOWPOS*>(l);
            RECT r=child.logical;
            int width=r.right-r.left, height=r.bottom-r.top;
            if (!(pos->flags&SWP_NOMOVE)) { r.left=pos->x; r.top=pos->y; }
            if (!(pos->flags&SWP_NOSIZE)) { width=pos->cx; height=pos->cy; }
            r.right=r.left+width; r.bottom=r.top+height; child.logical=r;
            auto mapped=d->child_rect(h,r);
            if (!(pos->flags&SWP_NOMOVE)) { pos->x=mapped.left; pos->y=mapped.top; }
            if (!(pos->flags&SWP_NOSIZE)) { pos->cx=mapped.right-mapped.left; pos->cy=mapped.bottom-mapped.top; }
        }
        if (msg==WM_SETFONT) {
            auto font=reinterpret_cast<HFONT>(w);
            if (!font) font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            if (GetObjectW(font,sizeof(child.font),&child.font)) {
                child.original_font=font;
                child.font_height=0; child.font_width=0;
                d->fit_child(h); return 0;
            }
        }
    }
    if (msg==WM_NCDESTROY) {
        RemoveWindowSubclass(h,child_proc,1);
        auto result=DefSubclassProc(h,msg,w,l);
        d->remove_child(h,false); return result;
    }
    return DefSubclassProc(h,msg,w,l);
}

Draw::~Draw() {
    close_settings();
    restore_overlay_input();
    if (settings_hook) { UnhookWindowsHookEx(settings_hook); settings_hook=nullptr; }
    if (shortcut_draw==this) shortcut_draw=nullptr;
    gpu.reset();
    if (clip_owned) ClipCursor(nullptr);
    while (!children.empty()) remove_child(children.begin()->first,true);
    if (mouse_draw==this) {
        if (mouse_slot && *mouse_slot==reinterpret_cast<void*>(&game_cursor)) {
            DWORD old=0;
            if (VirtualProtect(mouse_slot,sizeof(void*),PAGE_READWRITE,&old)) {
                InterlockedExchangePointer(mouse_slot,reinterpret_cast<void*>(original_cursor));
                DWORD ignored=0; VirtualProtect(mouse_slot,sizeof(void*),old,&ignored);
            }
        }
        mouse_draw=nullptr;
    }
    if (IsWindow(window)) {
        RemoveWindowSubclass(window,window_proc,1);
        if (auto menu=GetSystemMenu(window,FALSE)) {
            DeleteMenu(menu,MENU_FULLSCREEN,MF_BYCOMMAND); DeleteMenu(menu,MENU_WINDOWED,MF_BYCOMMAND); DeleteMenu(menu,MENU_SETTINGS,MF_BYCOMMAND);
        }
        if (styled) {
            SetWindowLongPtrW(window,GWL_STYLE,old_style); SetWindowLongPtrW(window,GWL_EXSTYLE,old_exstyle);
            SetWindowPos(window,nullptr,old_rect.left,old_rect.top,old_rect.right-old_rect.left,old_rect.bottom-old_rect.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        }
    }
    log("DirectDraw7 released");
}
bool Draw::hotkey(UINT msg, WPARAM w, LPARAM l) {
    if (w!=VK_RETURN) return false;
    if (msg==WM_SYSKEYDOWN && (l&(LPARAM(1)<<29))) {
        if (!(l&(LPARAM(1)<<30))) set_windowed(!windowed);
        eat_enter=true; return true;
    }
    if (msg==WM_SYSCHAR) return true; // suppress the default Alt+Enter beep
    if (eat_enter && (msg==WM_KEYUP || msg==WM_SYSKEYUP)) { eat_enter=false; return true; }
    return false;
}
LRESULT CALLBACK settings_keys(int code, WPARAM w, LPARAM l) {
    if (code>=0 && w==PM_REMOVE && shortcut_draw) {
        auto msg=reinterpret_cast<MSG*>(l);
        const bool target=msg->hwnd==shortcut_draw->window || IsChild(shortcut_draw->window,msg->hwnd) ||
            msg->hwnd==settings_window || (settings_window && IsChild(settings_window,msg->hwnd));
        const bool key=msg->wParam=='D' && (GetKeyState(VK_CONTROL)&0x8000) && (GetKeyState(VK_MENU)&0x8000);
        if (target && key && (msg->message==WM_KEYDOWN || msg->message==WM_SYSKEYDOWN)) {
            if (!(msg->lParam&(LPARAM(1)<<30))) {
                if (IsWindow(settings_window)) PostMessageW(settings_window,WM_CLOSE,0,0);
                else shortcut_draw->open_settings();
            }
            msg->message=WM_NULL;
        } else if (target && key && (msg->message==WM_KEYUP || msg->message==WM_SYSKEYUP)) msg->message=WM_NULL;
    }
    if (code>=0 && w==PM_REMOVE && IsWindow(settings_window)) {
        auto msg=reinterpret_cast<MSG*>(l);
        const bool in_overlay=msg->hwnd==settings_window || IsChild(settings_window,msg->hwnd);
        if(!in_overlay && shortcut_draw && (msg->hwnd==shortcut_draw->window || IsChild(shortcut_draw->window,msg->hwnd)) &&
           ((msg->message>=WM_KEYFIRST && msg->message<=WM_KEYLAST) || (msg->message>=WM_MOUSEFIRST && msg->message<=WM_MOUSELAST))) msg->message=WM_NULL;
        if (msg->hwnd==settings_window || IsChild(settings_window,msg->hwnd)) {
            if (IsDialogMessageW(settings_window,msg)) msg->message=WM_NULL;
        }
    }
    return CallNextHookEx(nullptr,code,w,l);
}
void Draw::open_settings() {
    if (IsWindow(settings_window)) { SetFocus(GetDlgItem(settings_window,IDC_WINDOWED)); return; }
    previous_focus=GetFocus();
    begin_overlay_input();
    overlay.integer_scaling=scaling==hq::Integer;
    overlay.background.clear();
    if (primary && !primary->busy()) {
        hq::Palette pal{}; if(primary->palette) pal=primary->palette->colors();
        overlay.background=hq::rgb(*primary->image,pal);
        overlay.image_width=primary->image->width; overlay.image_height=primary->image->height;
        for(auto& c:overlay.background) c=(((c>>16&255)*30/100)<<16)|(((c>>8&255)*30/100)<<8)|((c&255)*30/100);
    }
    opening_settings=true;
    settings=CreateDialogParamW(module,MAKEINTRESOURCEW(IDD_DISPLAY),window,settings_proc,reinterpret_cast<LPARAM>(this));
    opening_settings=false;
    if (!settings) { log("settings creation failed: %lu",GetLastError()); return; }
    settings_window=settings;
    sync_children();
    update_clip();
    ShowWindow(settings,SW_SHOW); SetFocus(GetDlgItem(settings,IDC_WINDOWED));
    begin_overlay_cursor();
    log("Display overlay opened");
}
void Draw::close_settings() {
    end_overlay_cursor();
    HWND h=settings; settings=nullptr;
    if (settings_window==h) settings_window=nullptr;
    if (IsWindow(h)) DestroyWindow(h);
    update_children_clipping();
    overlay.background.clear();
    if(IsWindow(window)) RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void Draw::apply_settings() {
    if (presenting || layout_busy) {
        SetDlgItemTextW(settings,IDC_STATUS,L"화면 전환 중입니다. 잠시 후 다시 적용해 주세요."); return;
    }
    const bool use_gpu=SendDlgItemMessageW(settings,IDC_RENDERER,CB_GETCURSEL,0,0)==0;
    const bool use_window=SendDlgItemMessageW(settings,IDC_MODE,CB_GETCURSEL,0,0)==0;
    gpu.reset(); gpu_reported=false; gpu_enabled=use_gpu; gpu_preferred=use_gpu;
    scaling=int(SendDlgItemMessageW(settings,IDC_SCALING,CB_GETCURSEL,0,0));
    if(scaling<0 || scaling>hq::Integer) scaling=hq::Nearest;
    overlay.integer_scaling=scaling==hq::Integer;
    vsync=IsDlgButtonChecked(settings,IDC_VSYNC)==BST_CHECKED;
    set_windowed(use_window); sync_children(); update_clip(); present();
    bool saved=true;
    const bool save=IsDlgButtonChecked(settings,IDC_SAVE)==BST_CHECKED;
    if (save) {
        const auto path=local_path(L"hqcdd.ini");
        saved=WritePrivateProfileStringW(L"Display",L"Fullscreen",windowed?L"0":L"1",path.c_str())!=FALSE;
        saved=(WritePrivateProfileStringW(L"Display",L"Renderer",use_gpu?L"auto":L"gdi",path.c_str())!=FALSE)&&saved;
        saved=(WritePrivateProfileStringW(L"Display",L"VSync",vsync?L"1":L"0",path.c_str())!=FALSE)&&saved;
        saved=(WritePrivateProfileStringW(L"Display",L"LinearFilter",scaling==hq::Bilinear?L"1":L"0",path.c_str())!=FALSE)&&saved;
    }
    if(save) {
        saved=(WritePrivateProfileStringW(L"Display",L"Scaling",hq::scaling_name(scaling),local_path(L"hqcdd.ini").c_str())!=FALSE)&&saved;
    }
    if (!saved) SetDlgItemTextW(settings,IDC_STATUS,L"현재 화면에 적용했습니다. 설정 파일 저장은 실패했습니다.");
    else if (use_gpu && !gpu_enabled) SetDlgItemTextW(settings,IDC_STATUS,L"GPU 출력을 사용할 수 없어 GDI로 적용했습니다.");
    else if(!use_gpu && (scaling==hq::Bilinear || scaling==hq::SharpBilinear)) SetDlgItemTextW(settings,IDC_STATUS,L"GDI에서는 Nearest로 출력합니다. 보간 필터는 GPU에서 적용됩니다.");
    else SetDlgItemTextW(settings,IDC_STATUS,save?L"적용하고 저장했습니다.":L"현재 실행에 적용했습니다. 파일에는 저장하지 않았습니다.");
    // Switching the owner's style can change z-order. Keep this owned window accessible.
    SetWindowPos(settings,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    overlay.layout(settings); InvalidateRect(settings,nullptr,FALSE); update_clip();
    log("Display settings applied: gpu=%d fullscreen=%d scaling=%d vsync=%d save=%d success=%d",use_gpu,!windowed,scaling,vsync,save,saved);
}
INT_PTR CALLBACK settings_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto d=reinterpret_cast<Draw*>(GetWindowLongPtrW(h,DWLP_USER));
    if (msg==WM_INITDIALOG) {
        d=reinterpret_cast<Draw*>(l); SetWindowLongPtrW(h,DWLP_USER,reinterpret_cast<LONG_PTR>(d));
        d->settings=h; settings_window=h;
        SendDlgItemMessageW(h,IDC_MODE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"창 모드"));
        SendDlgItemMessageW(h,IDC_MODE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"전체화면 (테두리 없음)"));
        SendDlgItemMessageW(h,IDC_MODE,CB_SETCURSEL,d->windowed?0:1,0);
        SendDlgItemMessageW(h,IDC_RENDERER,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"GPU (D3D11 · 자동 복구)"));
        SendDlgItemMessageW(h,IDC_RENDERER,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"호환 출력 (GDI)"));
        SendDlgItemMessageW(h,IDC_RENDERER,CB_SETCURSEL,d->gpu_preferred?0:1,0);
        for(auto name:{L"Nearest Neighbor",L"Bilinear",L"Sharp Bilinear",L"Integer"})
            SendDlgItemMessageW(h,IDC_SCALING,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendDlgItemMessageW(h,IDC_SCALING,CB_SETCURSEL,d->scaling,0);
        CheckDlgButton(h,IDC_VSYNC,d->vsync?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(h,IDC_SAVE,BST_CHECKED);
        EnableWindow(GetDlgItem(h,IDC_VSYNC),d->gpu_preferred);
        SetDlgItemTextW(h,IDC_STATUS,L"게임은 계속 진행됩니다.  ·  Esc 또는 닫기 버튼으로 닫기");
        d->overlay.init(h);
        EnableWindow(GetDlgItem(h,IDC_BILINEAR),d->gpu_preferred);
        EnableWindow(GetDlgItem(h,IDC_SHARP),d->gpu_preferred);
        return TRUE;
    }
    if (!d) return FALSE;
    Guard lock(mutex);
    if(msg==WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512))); return TRUE; }
    if(msg==WM_ERASEBKGND) return TRUE;
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps{}; auto dc=BeginPaint(h,&ps); d->overlay.paint(h,dc); EndPaint(h,&ps); return TRUE;
    }
    if(msg==WM_DRAWITEM) { d->overlay.button(h,*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE; }
    if (msg==WM_COMMAND) {
        int id=LOWORD(w);
        if(id==IDC_WINDOWED || id==IDC_FULLSCREEN || id==IDC_GPU || id==IDC_GDI) {
            bool mode=id==IDC_WINDOWED || id==IDC_FULLSCREEN;
            SendDlgItemMessageW(h,mode?IDC_MODE:IDC_RENDERER,CB_SETCURSEL,(id==IDC_WINDOWED || id==IDC_GPU)?0:1,0);
            if(!mode) SendMessageW(h,WM_COMMAND,MAKEWPARAM(IDC_RENDERER,CBN_SELCHANGE),0);
            RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); return TRUE;
        }
        if(id>=IDC_NEAREST && id<=IDC_INTEGER) {
            if((id==IDC_BILINEAR || id==IDC_SHARP) && SendDlgItemMessageW(h,IDC_RENDERER,CB_GETCURSEL,0,0)!=0) return TRUE;
            SendDlgItemMessageW(h,IDC_SCALING,CB_SETCURSEL,id-IDC_NEAREST,0);
            RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); return TRUE;
        }
        if(id==IDC_VSYNC || id==IDC_SAVE) {
            CheckDlgButton(h,id,IsDlgButtonChecked(h,id)==BST_CHECKED?BST_UNCHECKED:BST_CHECKED); return TRUE;
        }
        if (LOWORD(w)==IDC_RENDERER && HIWORD(w)==CBN_SELCHANGE) {
            const bool enabled=SendDlgItemMessageW(h,IDC_RENDERER,CB_GETCURSEL,0,0)==0;
            EnableWindow(GetDlgItem(h,IDC_BILINEAR),enabled); EnableWindow(GetDlgItem(h,IDC_SHARP),enabled); EnableWindow(GetDlgItem(h,IDC_VSYNC),enabled); return TRUE;
        }
        if (LOWORD(w)==IDC_APPLY) { d->apply_settings(); return TRUE; }
        if (LOWORD(w)==IDCANCEL) { SendMessageW(h,WM_CLOSE,0,0); return TRUE; }
    }
    if (msg==WM_CLOSE) {
        d->close_settings();
        if (IsWindow(d->window)) { SetForegroundWindow(d->window); SetFocus(IsWindow(d->previous_focus)?d->previous_focus:d->window); d->update_clip(); }
        return TRUE;
    }
    if (msg==WM_NCDESTROY) {
        end_overlay_cursor();
        if (settings_window==h) settings_window=nullptr;
        d->settings=nullptr;
        d->update_children_clipping();
    }
    return FALSE;
}
hq::Viewport Draw::viewport() const {
    RECT r{}; GetClientRect(window,&r);
    return hq::Viewport::fit(r.right,r.bottom,width,height,scaling==hq::Integer);
}
void Draw::update_clip() {
    const bool active=!settings && !windowed && IsWindow(window) && !IsIconic(window) && GetForegroundWindow()==window;
    if (active) {
        auto v=viewport(); RECT r{v.x,v.y,v.x+v.width,v.y+v.height};
        MapWindowPoints(window,HWND_DESKTOP,reinterpret_cast<POINT*>(&r),2);
        clip_owned=ClipCursor(&r)!=FALSE;
    } else if (clip_owned) { ClipCursor(nullptr); clip_owned=false; }
}
void Draw::set_windowed(bool value) {
    if (value==windowed || !IsWindow(window)) return;
    if (windowed) {
        windowed_placement.length=sizeof(windowed_placement);
        have_placement=GetWindowPlacement(window,&windowed_placement)!=FALSE;
    }
    windowed=value; resize(); sync_children(); update_clip(); present();
    log("Presentation mode: %s",windowed?"windowed":"borderless fullscreen");
}
void Draw::resize() {
    if (!IsWindow(window) || layout_busy) return;
    layout_busy=true;
    if (!styled) {
        old_style=GetWindowLongPtrW(window,GWL_STYLE); old_exstyle=GetWindowLongPtrW(window,GWL_EXSTYLE);
        GetWindowRect(window,&old_rect); styled=true;
    }
    const DWORD style=windowed?WINDOW_STYLE:(WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN);
    SetWindowLongPtrW(window,GWL_STYLE,style); SetWindowLongPtrW(window,GWL_EXSTYLE,0);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    if (!windowed) {
        const auto& r=monitor.rcMonitor;
        // Clear maximized state before applying the full monitor rectangle.
        ShowWindow(window,SW_SHOWNORMAL);
        SetWindowPos(window,HWND_TOP,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_FRAMECHANGED|SWP_NOACTIVATE);
    } else if (have_placement) {
        SetWindowPlacement(window,&windowed_placement);
        SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);
    } else {
        RECT r{0,0,width,height}; AdjustWindowRectEx(&r,style,FALSE,0);
        int w=std::min(r.right-r.left,monitor.rcWork.right-monitor.rcWork.left);
        int h=std::min(r.bottom-r.top,monitor.rcWork.bottom-monitor.rcWork.top);
        int x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-w)/2;
        int y=monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-h)/2;
        SetWindowPos(window,HWND_NOTOPMOST,x,y,w,h,SWP_FRAMECHANGED|SWP_NOACTIVATE);
    }
    if (auto menu=GetSystemMenu(window,FALSE)) {
        CheckMenuItem(menu,MENU_FULLSCREEN,MF_BYCOMMAND|(windowed?MF_UNCHECKED:MF_CHECKED));
        CheckMenuItem(menu,MENU_WINDOWED,MF_BYCOMMAND|(windowed?MF_CHECKED:MF_UNCHECKED));
    }
    layout_busy=false;
    if(settings) overlay.layout(settings);
    sync_children(); update_clip();
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}

RECT Draw::child_rect(HWND child, RECT logical) const {
    auto v=viewport();
    if (GetParent(child)!=window) { v.x=0; v.y=0; }
    return {v.map_x(logical.left),v.map_y(logical.top),v.map_x(logical.right),v.map_y(logical.bottom)};
}
void Draw::add_child(HWND h) {
    if(opening_settings || h==settings || (settings && IsChild(settings,h))) return;
    if (!IsWindow(h) || children.count(h) || !IsChild(window,h)) return;
    Child c; GetWindowRect(h,&c.logical);
    MapWindowPoints(HWND_DESKTOP,GetParent(h),reinterpret_cast<POINT*>(&c.logical),2);
    c.original_font=reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0));
    if (!c.original_font) c.original_font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    GetObjectW(c.original_font,sizeof(c.font),&c.font);
    children.emplace(h,c);
    if (!SetWindowSubclass(h,child_proc,1,reinterpret_cast<DWORD_PTR>(this))) { children.erase(h); return; }
    update_child_clipping(h);
    if (settings) SetWindowPos(settings,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    // Defer positioning until CreateWindow/WM_CREATE has fully returned.
    PostMessageW(window,WM_HQ_LAYOUT,0,0);
}
void Draw::remove_child(HWND h, bool restore) {
    auto it=children.find(h); if (it==children.end()) return;
    const auto c=it->second;
    children.erase(it);
    if (restore && IsWindow(h)) {
        RemoveWindowSubclass(h,child_proc,1);
        if (c.overlay_clipping) {
            const auto style=GetWindowLongPtrW(h,GWL_STYLE)&~LONG_PTR(WS_CLIPSIBLINGS);
            SetWindowLongPtrW(h,GWL_STYLE,style|(c.original_clip_siblings?WS_CLIPSIBLINGS:0));
        }
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(c.original_font),TRUE);
        SetWindowPos(h,nullptr,c.logical.left,c.logical.top,c.logical.right-c.logical.left,c.logical.bottom-c.logical.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    if (c.scaled_font) DeleteObject(c.scaled_font);
}
void Draw::update_child_clipping(HWND h) {
    auto it=children.find(h); if (it==children.end() || !IsWindow(h)) return;
    auto& c=it->second;
    // Only the game's direct children are siblings of the full-client overlay.
    const bool enabled=IsWindow(settings) && GetParent(h)==window;
    if (enabled==c.overlay_clipping) return;
    const auto style=GetWindowLongPtrW(h,GWL_STYLE);
    if (enabled) c.original_clip_siblings=(style&WS_CLIPSIBLINGS)!=0;
    c.overlay_clipping=enabled;
    SetWindowLongPtrW(h,GWL_STYLE,(style&~LONG_PTR(WS_CLIPSIBLINGS))|
        ((enabled || c.original_clip_siblings)?WS_CLIPSIBLINGS:0));
}
void Draw::update_children_clipping() {
    // Do not enumerate/register windows while the settings dialog is being destroyed.
    std::vector<HWND> list;
    for (const auto& entry:children) list.push_back(entry.first);
    for (auto h:list) update_child_clipping(h);
}
void Draw::fit_child(HWND h) {
    auto it=children.find(h); if (it==children.end()) return;
    auto& c=it->second; const bool previous=layout_busy; layout_busy=true;
    auto r=child_rect(h,c.logical);
    RECT current{}; GetWindowRect(h,&current); MapWindowPoints(HWND_DESKTOP,GetParent(h),reinterpret_cast<POINT*>(&current),2);
    if (!EqualRect(&r,&current))
        SetWindowPos(h,nullptr,r.left,r.top,std::max(1L,r.right-r.left),std::max(1L,r.bottom-r.top),SWP_NOZORDER|SWP_NOACTIVATE);
    const auto v=viewport();
    LOGFONTW font=c.font;
    font.lfHeight=MulDiv(font.lfHeight,v.height,height); font.lfWidth=MulDiv(font.lfWidth,v.width,width);
    if (!font.lfHeight) font.lfHeight=-1;
    if (font.lfHeight!=c.font_height || font.lfWidth!=c.font_width) {
        HFONT scaled=CreateFontIndirectW(&font);
        if (scaled) {
            SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(scaled),TRUE);
            if (c.scaled_font) DeleteObject(c.scaled_font);
            c.scaled_font=scaled; c.font_height=font.lfHeight; c.font_width=font.lfWidth;
        }
    }
    layout_busy=previous;
}
void Draw::sync_children() {
    if (layout_busy || !IsWindow(window)) return;
    if (GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()) return;
    EnumChildWindows(window,[](HWND h,LPARAM data)->BOOL { reinterpret_cast<Draw*>(data)->add_child(h); return TRUE; },reinterpret_cast<LPARAM>(this));
    std::vector<HWND> list;
    for (auto& entry:children) list.push_back(entry.first);
    for (auto h:list) {
        if (IsWindow(h)) { update_child_clipping(h); fit_child(h); }
        else remove_child(h,false);
    }
}
void Draw::present() {
    if (presenting || layout_busy || !primary || primary->busy() || !IsWindow(window) || IsIconic(window)) return;
    presenting=true;
    struct Reset { bool& b; ~Reset(){b=false;} } reset{presenting};
    try {
        sync_children();
        hq::Palette pal{};
        if (primary->palette) pal=primary->palette->colors();
        if (gpu_enabled) {
            if (!gpu) gpu=std::make_unique<hq::Gpu>();
            HRESULT hr=gpu->present(window,*primary->image,pal,viewport(),vsync,scaling);
            if (SUCCEEDED(hr)) {
                if (!gpu_reported) { log("D3D11 hardware presentation active"); gpu_reported=true; }
                return;
            }
            log("D3D11 failed HRESULT=%08lx; switching to GDI",hr);
            gpu.reset(); gpu_enabled=false;
            RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
        }
        auto pixels=hq::rgb(*primary->image,pal);
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=primary->image->width; info.bmiHeader.biHeight=-primary->image->height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        HDC dc=GetDCEx(window,nullptr,DCX_CACHE|DCX_CLIPCHILDREN); if (!dc) return;
        // Also exclude child rectangles explicitly: keep native EDIT text, borders,
        // selection and caret intact during repeated Flip/Unlock/palette repaint.
        for (HWND child=GetWindow(window,GW_CHILD); child; child=GetWindow(child,GW_HWNDNEXT)) {
            if (!IsWindowVisible(child)) continue;
            RECT r{}; GetWindowRect(child,&r);
            MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&r),2);
            ExcludeClipRect(dc,r.left,r.top,r.right,r.bottom);
        }
        const auto v=viewport();
        RECT client{}; GetClientRect(window,&client);
        // Fill only bars, avoiding a black flash underneath the next frame.
        const RECT bars[]={{0,0,client.right,v.y},{0,v.y+v.height,client.right,client.bottom},
                           {0,v.y,v.x,v.y+v.height},{v.x+v.width,v.y,client.right,v.y+v.height}};
        for (auto& bar:bars) FillRect(dc,&bar,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetStretchBltMode(dc,COLORONCOLOR);
        StretchDIBits(dc,v.x,v.y,v.width,v.height,0,0,primary->image->width,
                      primary->image->height,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        ::ReleaseDC(window,dc);
    } catch (const std::bad_alloc&) { log("presentation allocation failed"); }
}
HRESULT Draw::SetCooperativeLevel(HWND h, DWORD flags) {
    Guard lock(mutex);
    log("SetCooperativeLevel hwnd=%p flags=%08lx",h,flags);
    if (!IsWindow(h)) return DDERR_INVALIDPARAMS;
    if (window && window!=h) return DDERR_HWNDALREADYSET;
    // SetWindowSubclass must run on the window's owning thread.
    if (GetWindowThreadProcessId(h,nullptr)!=GetCurrentThreadId()) return DDERR_INVALIDPARAMS;
    if (!SetWindowSubclass(h,window_proc,1,reinterpret_cast<DWORD_PTR>(this))) return E_FAIL;
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&window_proc),&pinned);
    window=h;
    hook_overlay_input();
    shortcut_draw=this;
    if (!settings_hook) settings_hook=SetWindowsHookExW(WH_GETMESSAGE,settings_keys,nullptr,GetCurrentThreadId());
    if (auto menu=GetSystemMenu(window,FALSE)) {
        DeleteMenu(menu,MENU_FULLSCREEN,MF_BYCOMMAND); DeleteMenu(menu,MENU_WINDOWED,MF_BYCOMMAND); DeleteMenu(menu,MENU_SETTINGS,MF_BYCOMMAND);
        AppendMenuW(menu,MF_STRING,MENU_SETTINGS,L"디스플레이 설정 (Ctrl+Alt+D)");
        AppendMenuW(menu,MF_STRING,MENU_FULLSCREEN,L"전체화면 (Alt+Enter)");
        AppendMenuW(menu,MF_STRING,MENU_WINDOWED,L"창 모드 (Alt+Enter)");
    }
    if (!hook_cursor(this)) log("GetCursorPos import not found; host may already use client coordinates");
    resize(); return DD_OK;
}

BOOL WINAPI game_cursor(LPPOINT point) {
    const BOOL ok=original_cursor(point);
    Guard lock(mutex);
    if (ok && point && mouse_draw && IsWindow(mouse_draw->window)) {
        ScreenToClient(mouse_draw->window,point);
        auto v=mouse_draw->viewport();
        point->x=std::clamp(v.unmap_x(point->x),0,mouse_draw->width-1);
        point->y=std::clamp(v.unmap_y(point->y),0,mouse_draw->height-1);
    }
    return ok;
}
struct InputSlot { void** slot; void* original; void* replacement; };
std::vector<InputSlot> input_slots;
using KeyFn=SHORT (WINAPI*)(int);
using KeyboardFn=BOOL (WINAPI*)(PBYTE);
KeyFn original_async=::GetAsyncKeyState, original_key=::GetKeyState;
KeyboardFn original_keyboard=::GetKeyboardState;
using ShowCursorFn=int (WINAPI*)(BOOL);
using SetCursorFn=HCURSOR (WINAPI*)(HCURSOR);
ShowCursorFn original_show_cursor=::ShowCursor;
SetCursorFn original_set_cursor=::SetCursor;
bool overlay_cursor_active=false;
int cursor_lift=0;
HCURSOR game_cursor_shape=nullptr;
void begin_overlay_cursor() {
    if(overlay_cursor_active) return;
    overlay_cursor_active=true;
    game_cursor_shape=GetCursor();
    // ShowCursor has a counter, not a boolean state. Add only what is necessary.
    int count=original_show_cursor(TRUE); ++cursor_lift;
    if(count>0) { original_show_cursor(FALSE); --cursor_lift; }
    else while(count<0) { count=original_show_cursor(TRUE); ++cursor_lift; }
    original_set_cursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
}
void end_overlay_cursor() {
    if(!overlay_cursor_active) return;
    overlay_cursor_active=false;
    while(cursor_lift>0) { original_show_cursor(FALSE); --cursor_lift; }
    original_set_cursor(game_cursor_shape);
}
int WINAPI overlay_show_cursor(BOOL show) {
    int count=original_show_cursor(show);
    if(!overlay_cursor_active) return count;
    const int game_count=count-cursor_lift;
    while(count<0) { count=original_show_cursor(TRUE); ++cursor_lift; }
    return game_count;
}
HCURSOR WINAPI overlay_set_cursor(HCURSOR cursor) {
    if(!overlay_cursor_active) return original_set_cursor(cursor);
    HCURSOR previous=game_cursor_shape; game_cursor_shape=cursor;
    original_set_cursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512))); return previous;
}
bool swallowed_keys[256]{};
void begin_overlay_input() { std::fill(std::begin(swallowed_keys),std::end(swallowed_keys),true); }
SHORT filter_key(int key,SHORT result) {
    if(key<0 || key>255) return result;
    if(settings_window) { swallowed_keys[key]=true; return 0; }
    if(swallowed_keys[key]) { if(!(result&0x8000)) swallowed_keys[key]=false; return 0; }
    return result;
}
SHORT WINAPI overlay_async(int key) { return filter_key(key,original_async(key)); }
SHORT WINAPI overlay_key(int key) { return filter_key(key,original_key(key)); }
BOOL WINAPI overlay_keyboard(PBYTE keys) {
    BOOL ok=original_keyboard(keys);
    if(ok) for(int i=0;i<256;++i) {
        if(settings_window || swallowed_keys[i]) { filter_key(i,SHORT((keys[i]&0x80)<<8)); keys[i]=0; }
    }
    return ok;
}
void restore_overlay_input() {
    for(auto& i:input_slots) if(*i.slot==i.replacement) {
        DWORD old=0;
        if(VirtualProtect(i.slot,sizeof(void*),PAGE_READWRITE,&old)) {
            InterlockedExchangePointer(i.slot,i.original); DWORD ignored=0; VirtualProtect(i.slot,sizeof(void*),old,&ignored);
        }
    }
    input_slots.clear();
}
void hook_overlay_input() {
    if(!input_slots.empty()) return;
    auto base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;imports->Name;++imports) {
        if(_stricmp(reinterpret_cast<char*>(base+imports->Name),"user32.dll") || !imports->OriginalFirstThunk) continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->OriginalFirstThunk);
        auto addresses=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->FirstThunk);
        for(;names->u1.AddressOfData;++names,++addresses) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData)->Name;
            auto slot=reinterpret_cast<void**>(&addresses->u1.Function); void* replacement=nullptr;
            if(!strcmp(reinterpret_cast<char*>(name),"GetAsyncKeyState")) { original_async=reinterpret_cast<KeyFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_async); }
            if(!strcmp(reinterpret_cast<char*>(name),"GetKeyState")) { original_key=reinterpret_cast<KeyFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_key); }
            if(!strcmp(reinterpret_cast<char*>(name),"GetKeyboardState")) { original_keyboard=reinterpret_cast<KeyboardFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_keyboard); }
            if(!strcmp(reinterpret_cast<char*>(name),"ShowCursor")) { original_show_cursor=reinterpret_cast<ShowCursorFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_show_cursor); }
            if(!strcmp(reinterpret_cast<char*>(name),"SetCursor")) { original_set_cursor=reinterpret_cast<SetCursorFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_set_cursor); }
            if(!replacement) continue;
            DWORD old=0;
            if(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) {
                input_slots.push_back({slot,*slot,replacement});
                InterlockedExchangePointer(slot,replacement); DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
            }
        }
    }
}
bool hook_cursor(Draw* d) {
    if (mouse_draw && mouse_draw!=d) return false;
    auto base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return false;
    auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);
    for (;imports->Name;++imports) {
        if (_stricmp(reinterpret_cast<char*>(base+imports->Name),"user32.dll") || !imports->OriginalFirstThunk) continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->OriginalFirstThunk);
        auto functions=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->FirstThunk);
        for (;names->u1.AddressOfData;++names,++functions) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<char*>(name->Name),"GetCursorPos")) continue;
            auto slot=reinterpret_cast<void**>(&functions->u1.Function);
            if (*slot==reinterpret_cast<void*>(&game_cursor)) return true;
            DWORD old=0;
            if (!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) return false;
            original_cursor=reinterpret_cast<CursorFn>(*slot);
            InterlockedExchangePointer(slot,reinterpret_cast<void*>(&game_cursor));
            DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
            mouse_slot=slot; mouse_draw=d;
            log("GetCursorPos: game-module IAT mapped to client coordinates"); return true;
        }
    }
    return false;
}
HRESULT Draw::SetDisplayMode(DWORD w, DWORD h, DWORD bits, DWORD, DWORD flags) {
    Guard lock(mutex);
    log("SetDisplayMode %lux%lu %lu-bit flags=%08lx",w,h,bits,flags);
    if (flags || w<1 || w>8192 || h<1 || h>8192 || (bits!=8 && bits!=16 && bits!=32)) return DDERR_INVALIDMODE;
    if (!window) return DDERR_NOCOOPERATIVELEVELSET;
    width=w; height=h; bpp=bits; resize(); return DD_OK;
}
HRESULT Draw::GetDisplayMode(DDSURFACEDESC2* out) {
    Guard lock(mutex); if (!out || out->dwSize!=sizeof(*out)) return DDERR_INVALIDPARAMS;
    *out={}; out->dwSize=sizeof(*out); out->dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;
    out->dwWidth=width; out->dwHeight=height; out->lPitch=(width*(bpp/8)+3)&~3;
    out->ddpfPixelFormat=pixel_format(bpp); out->dwRefreshRate=60; return DD_OK;
}
HRESULT Draw::EnumDisplayModes(DWORD flags, DDSURFACEDESC2* filter, void* ctx, LPDDENUMMODESCALLBACK2 cb) {
    if (!cb || (flags & ~(DDEDM_REFRESHRATES|DDEDM_STANDARDVGAMODES))) return DDERR_INVALIDPARAMS;
    for (const SIZE size : {SIZE{320,200},SIZE{640,480},SIZE{800,600},SIZE{1024,768},SIZE{1280,1024}})
        for (DWORD bits : {8u,16u,32u}) {
            DDSURFACEDESC2 d{}; d.dwSize=sizeof(d); d.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;
            d.dwWidth=size.cx; d.dwHeight=size.cy; d.ddpfPixelFormat=pixel_format(bits); d.dwRefreshRate=60;
            if (filter && (((filter->dwFlags&DDSD_WIDTH) && filter->dwWidth!=d.dwWidth) ||
                ((filter->dwFlags&DDSD_HEIGHT) && filter->dwHeight!=d.dwHeight) ||
                ((filter->dwFlags&DDSD_PIXELFORMAT) && filter->ddpfPixelFormat.dwRGBBitCount!=bits))) continue;
            if (cb(&d,ctx)==DDENUMRET_CANCEL) return DD_OK;
        }
    return DD_OK;
}
HRESULT Draw::GetCaps(DDCAPS* driver, DDCAPS* hel) {
    for (auto out : {driver,hel}) if (out) {
        if (out->dwSize<sizeof(DWORD) || out->dwSize>sizeof(DDCAPS)) return DDERR_INVALIDPARAMS;
        DDCAPS c{}; c.dwSize=out->dwSize;
        c.dwCaps=DDCAPS_BLT|DDCAPS_BLTCOLORFILL|DDCAPS_BLTSTRETCH|DDCAPS_PALETTE|DDCAPS_GDI;
        c.dwCKeyCaps=DDCKEYCAPS_SRCBLT|DDCKEYCAPS_DESTBLT;
        c.dwPalCaps=DDPCAPS_8BIT|DDPCAPS_ALLOW256;
        c.dwVidMemTotal=c.dwVidMemFree=128*1024*1024;
        c.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY|DDSCAPS_FLIP;
        std::memcpy(out,&c,c.dwSize);
    }
    return DD_OK;
}
HRESULT Draw::GetGDISurface(IDirectDrawSurface7** out) {
    Guard lock(mutex); if (!out) return E_POINTER; *out=primary;
    if (!primary) return DDERR_NOTFOUND; primary->AddRef(); return DD_OK;
}
HRESULT Draw::CreateClipper(DWORD flags, IDirectDrawClipper** out, IUnknown* outer) {
    if (!out) return E_POINTER; *out=nullptr;
    // Use the same absolute system DLL as the legacy multimedia entry point.
    using Fn=HRESULT (WINAPI*)(DWORD,IDirectDrawClipper**,IUnknown*);
    static Fn fn=reinterpret_cast<Fn>(system_ddraw_proc("DirectDrawCreateClipper"));
    return fn ? fn(flags,out,outer) : E_FAIL;
}
HRESULT Draw::CreatePalette(DWORD flags, PALETTEENTRY* e, IDirectDrawPalette** out, IUnknown* outer) {
    Guard lock(mutex); if (!out) return E_POINTER; *out=nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!e || !(flags&DDPCAPS_8BIT) || (flags&(DDPCAPS_1BIT|DDPCAPS_2BIT|DDPCAPS_4BIT|DDPCAPS_8BITENTRIES))) return DDERR_INVALIDPARAMS;
    try { *out=new Palette(this,flags,e); log("CreatePalette flags=%08lx",flags); return DD_OK; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}
HRESULT Draw::CreateSurface(DDSURFACEDESC2* d, IDirectDrawSurface7** out, IUnknown* outer) {
    Guard lock(mutex); if (!out) return E_POINTER; *out=nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!d || d->dwSize!=sizeof(*d) || !(d->dwFlags&DDSD_CAPS)) return DDERR_INVALIDPARAMS;
    const DWORD c=d->ddsCaps.dwCaps;
    log("CreateSurface flags=%08lx caps=%08lx size=%lux%lu back=%lu",d->dwFlags,c,d->dwWidth,d->dwHeight,d->dwBackBufferCount);
    if (c&(DDSCAPS_OVERLAY|DDSCAPS_TEXTURE|DDSCAPS_ZBUFFER) || (d->dwFlags&DDSD_LPSURFACE)) return DDERR_UNSUPPORTED;
    const bool front=(c&DDSCAPS_PRIMARYSURFACE)!=0;
    if (front && primary) return DDERR_PRIMARYSURFACEALREADYEXISTS;
    if (front && !window) return DDERR_NOCOOPERATIVELEVELSET;
    if (!front && (!(d->dwFlags&DDSD_WIDTH) || !(d->dwFlags&DDSD_HEIGHT))) return DDERR_INVALIDPARAMS;
    int bits=bpp;
    if (d->dwFlags&DDSD_PIXELFORMAT) {
        const auto& p=d->ddpfPixelFormat; bits=p.dwRGBBitCount;
        auto expected=pixel_format(bits);
        if (p.dwSize!=sizeof(p) || !(p.dwFlags&DDPF_RGB) ||
            (bits==8 && !(p.dwFlags&DDPF_PALETTEINDEXED8)) ||
            (bits!=8 && (p.dwRBitMask!=expected.dwRBitMask || p.dwGBitMask!=expected.dwGBitMask || p.dwBBitMask!=expected.dwBBitMask))) return DDERR_INVALIDPIXELFORMAT;
    }
    const DWORD backs=(d->dwFlags&DDSD_BACKBUFFERCOUNT)?d->dwBackBufferCount:0;
    if (backs>1 || ((c&DDSCAPS_FLIP) && (!front || backs!=1)) || (backs && !(c&DDSCAPS_FLIP))) return DDERR_UNSUPPORTED;
    Surface* s=nullptr;
    try {
        s=new Surface(this,front?width:d->dwWidth,front?height:d->dwHeight,bits,(c&~DDSCAPS_VIDEOMEMORY)|DDSCAPS_SYSTEMMEMORY);
        if (backs) s->back=new Surface(this,width,height,bits,DDSCAPS_BACKBUFFER|DDSCAPS_FLIP|DDSCAPS_SYSTEMMEMORY);
        if (front) primary=s;
        *out=s; return DD_OK;
    } catch (const std::bad_alloc&) { if(s) s->Release(); return E_OUTOFMEMORY; }
      catch (const std::invalid_argument&) { if(s) s->Release(); return DDERR_INVALIDPARAMS; }
}
HRESULT Palette::SetEntries(DWORD flags, DWORD base, DWORD count, PALETTEENTRY* in) {
    Guard lock(mutex);
    if (flags || !in || base>256 || count>256-base) return DDERR_INVALIDPARAMS;
    std::memcpy(entries+base,in,count*sizeof(PALETTEENTRY));
    for (auto s : surfaces) if (s->dc && s->image->bpp==8 &&
        (s->palette==this || (!s->palette && s->draw==draw && draw->primary && draw->primary->palette==this))) {
        RGBQUAD table[256]{};
        for (DWORD i=0;i<count;++i) table[i]={in[i].peBlue,in[i].peGreen,in[i].peRed,0};
        SetDIBColorTable(s->dc,base,count,table);
    }
    // Repaint even when no pixels changed: fades/cycling update only palette entries.
    if (draw->primary && draw->primary->palette==this) draw->present();
    return DD_OK;
}
HRESULT Surface::Blt(RECT* dest, IDirectDrawSurface7* source, RECT* src, DWORD flags, DDBLTFX* fx) {
    Guard lock(mutex);
    constexpr DWORD allowed=DDBLT_WAIT|DDBLT_DONOTWAIT|DDBLT_ASYNC|DDBLT_COLORFILL|DDBLT_KEYSRC|DDBLT_KEYDEST|DDBLT_KEYSRCOVERRIDE|DDBLT_KEYDESTOVERRIDE|DDBLT_DDFX|DDBLT_ROP;
    if (flags&~allowed) return unsupported("Blt flags");
    auto s=static_cast<Surface*>(source);
    if (s && (!surfaces.count(s) || s->draw!=draw)) return DDERR_INVALIDOBJECT;
    if (busy() || (s && s->busy())) return DDERR_SURFACEBUSY;
    if ((flags&(DDBLT_COLORFILL|DDBLT_KEYSRCOVERRIDE|DDBLT_KEYDESTOVERRIDE|DDBLT_DDFX|DDBLT_ROP)) && (!fx || fx->dwSize!=sizeof(*fx))) return DDERR_INVALIDPARAMS;
    if ((flags&DDBLT_ROP) && fx->dwROP!=SRCCOPY) return unsupported("Blt ROP");
    if ((flags&DDBLT_DDFX) && (fx->dwDDFX&~(DDBLTFX_MIRRORLEFTRIGHT|DDBLTFX_MIRRORUPDOWN))) return unsupported("Blt DDFX");
    auto dr=rect(dest,image->width,image->height);
    if (!hq::valid_rect(dr)) return DDERR_INVALIDRECT;
    // Clipper HWND means legacy primary Blt rectangles are in desktop coordinates.
    if (primary() && dest && clipper) {
        HWND h=nullptr;
        if (SUCCEEDED(clipper->GetHWnd(&h)) && h==draw->window) {
            POINT p{}; ClientToScreen(h,&p); dr.left-=p.x; dr.right-=p.x; dr.top-=p.y; dr.bottom-=p.y;
            const auto v=draw->viewport();
            dr={v.unmap_x(dr.left),v.unmap_y(dr.top),v.unmap_x(dr.right),v.unmap_y(dr.bottom)};
        } else return unsupported("Blt explicit clip region");
    }
    try {
        if (flags&DDBLT_COLORFILL) hq::fill(*image,dr,fx->dwFillColor);
        else {
            if (!s) return DDERR_INVALIDPARAMS;
            if (image->bpp!=s->image->bpp) return DDERR_INVALIDPIXELFORMAT;
            auto sr=rect(src,s->image->width,s->image->height);
            if (!hq::valid_rect(sr) || sr.left<0 || sr.top<0 || sr.right>s->image->width || sr.bottom>s->image->height) return DDERR_INVALIDRECT;
            bool sk=(flags&(DDBLT_KEYSRC|DDBLT_KEYSRCOVERRIDE))!=0, dk=(flags&(DDBLT_KEYDEST|DDBLT_KEYDESTOVERRIDE))!=0;
            if ((flags&DDBLT_KEYSRC) && !s->has_src_key) return DDERR_NOCOLORKEY;
            if ((flags&DDBLT_KEYDEST) && !has_dst_key) return DDERR_NOCOLORKEY;
            auto skey=(flags&DDBLT_KEYSRCOVERRIDE)?fx->ddckSrcColorkey:s->src_key;
            auto dkey=(flags&DDBLT_KEYDESTOVERRIDE)?fx->ddckDestColorkey:dst_key;
            bool mx=(flags&DDBLT_DDFX) && (fx->dwDDFX&DDBLTFX_MIRRORLEFTRIGHT);
            bool my=(flags&DDBLT_DDFX) && (fx->dwDDFX&DDBLTFX_MIRRORUPDOWN);
            hq::blit(*image,dr,*s->image,sr,sk,skey.dwColorSpaceLowValue,skey.dwColorSpaceHighValue,dk,dkey.dwColorSpaceLowValue,dkey.dwColorSpaceHighValue,mx,my);
        }
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (const std::invalid_argument&) { return DDERR_INVALIDRECT; }
    if (primary()) draw->present(); return DD_OK;
}
HRESULT Surface::BltFast(DWORD x, DWORD y, IDirectDrawSurface7* source, RECT* src, DWORD flags) {
    Guard lock(mutex); auto s=static_cast<Surface*>(source);
    if (!s || !surfaces.count(s)) return DDERR_INVALIDOBJECT;
    if (flags&~(DDBLTFAST_WAIT|DDBLTFAST_DONOTWAIT|DDBLTFAST_SRCCOLORKEY|DDBLTFAST_DESTCOLORKEY)) return DDERR_UNSUPPORTED;
    if (clipper) return DDERR_BLTFASTCANTCLIP;
    auto r=rect(src,s->image->width,s->image->height);
    if (!hq::valid_rect(r) || x>DWORD(image->width) || y>DWORD(image->height) ||
        int64_t(x)+r.right-r.left>image->width || int64_t(y)+r.bottom-r.top>image->height) return DDERR_INVALIDRECT;
    RECT dest{LONG(x),LONG(y),LONG(x+r.right-r.left),LONG(y+r.bottom-r.top)};
    DWORD f=DDBLT_WAIT;
    if (flags&DDBLTFAST_SRCCOLORKEY) f|=DDBLT_KEYSRC;
    if (flags&DDBLTFAST_DESTCOLORKEY) f|=DDBLT_KEYDEST;
    return Blt(&dest,source,src,f,nullptr);
}
HRESULT Surface::GetDC(HDC* out) {
    Guard lock(mutex); if (!out) return E_POINTER; *out=nullptr;
    if (busy()) return DDERR_SURFACEBUSY;
    struct Info { BITMAPINFOHEADER header; RGBQUAD colors[256]; } info{};
    info.header.biSize=sizeof(BITMAPINFOHEADER); info.header.biWidth=image->width;
    info.header.biHeight=-image->height; info.header.biPlanes=1; info.header.biBitCount=WORD(image->bpp);
    info.header.biCompression=image->bpp==16?BI_BITFIELDS:BI_RGB;
    if (image->bpp==16) { DWORD masks[3]{0xf800,0x7e0,0x1f}; std::memcpy(info.colors,masks,sizeof(masks)); }
    if (image->bpp==8) {
        info.header.biClrUsed=256;
        auto p=palette?palette:(draw->primary?draw->primary->palette:nullptr);
        if (p) for (int i=0;i<256;++i) info.colors[i]={p->entries[i].peBlue,p->entries[i].peGreen,p->entries[i].peRed,0};
    }
    dc=CreateCompatibleDC(nullptr);
    if (!dc) return E_OUTOFMEMORY;
    bitmap=CreateDIBSection(dc,reinterpret_cast<BITMAPINFO*>(&info),DIB_RGB_COLORS,&dib_bits,nullptr,0);
    if (!bitmap) { DeleteDC(dc); dc=nullptr; return E_OUTOFMEMORY; }
    old_bitmap=SelectObject(dc,bitmap);
    std::memcpy(dib_bits,image->bytes.data(),image->bytes.size());
    *out=dc; return DD_OK;
}
} // namespace

extern "C" HRESULT WINAPI DirectDrawCreateEx(GUID* guid, void** out, REFIID iid, IUnknown* outer) {
    Guard lock(mutex);
    if (!out) return E_POINTER; *out=nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (iid!=IID_IDirectDraw7) return E_NOINTERFACE;
    if (guid) return DDERR_INVALIDDIRECTDRAWGUID;
    try { *out=static_cast<IDirectDraw7*>(new Draw); return DD_OK; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}
extern "C" HRESULT WINAPI DirectDrawCreate(GUID* guid, IDirectDraw** out, IUnknown* outer) {
    // AMStream needs the legacy IDirectDraw ABI, not our IDirectDraw7 implementation.
    using Fn=HRESULT (WINAPI*)(GUID*,IDirectDraw**,IUnknown*);
    static Fn fn=reinterpret_cast<Fn>(system_ddraw_proc("DirectDrawCreate"));
    if(!fn) { if(out) *out=nullptr; return E_FAIL; }
    return fn(guid,out,outer);
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) { module=h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
