#include "osd.h"
#include "osd_stats.h"
#include <psapi.h>
#include <atomic>
#include <mutex>
#include <cstdio>

namespace hq {
double PerformanceOsd::now() {
    static const double frequency=[] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
    LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return double(t.QuadPart)*1000/frequency;
}
struct PerformanceOsd::Impl {
    HWND owner=nullptr,window=nullptr;
    HINSTANCE module=nullptr;
    std::atomic<bool> active{false};
    std::atomic<bool> collecting{false};
    bool blocked=false,was_foreground=true;
    std::mutex mutex;
    OsdStats stats;
    std::array<double,240> waits{}; size_t wait_next=0,wait_count=0;
    double last_wait=0;
    bool gpu=false,vsync=false; int scaling=0,width=0,height=0;
    double cpu_percent=0,ram_mb=0,last_system=0; ULONGLONG last_cpu=0;
    HFONT font=nullptr,title_font=nullptr;
    static LRESULT CALLBACK proc(HWND h,UINT message,WPARAM w,LPARAM l) {
        auto p=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(message==WM_NCCREATE) { p=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p)); }
        if(!p) return DefWindowProcW(h,message,w,l);
        if(message==WM_NCHITTEST) return HTTRANSPARENT;
        if(message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if(message==WM_ERASEBKGND) return 1;
        if(message==WM_TIMER) { p->refresh(); return 0; }
        if(message==WM_PAINT) { PAINTSTRUCT ps{}; auto dc=BeginPaint(h,&ps); p->paint(dc); EndPaint(h,&ps); return 0; }
        if(message==WM_NCDESTROY) { p->window=nullptr; SetWindowLongPtrW(h,GWLP_USERDATA,0); }
        return DefWindowProcW(h,message,w,l);
    }
    bool create() {
        if(window) return true;
        WNDCLASSW wc{}; wc.hInstance=module; wc.lpfnWndProc=proc; wc.lpszClassName=L"HQCDD.PerformanceOSD";
        if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
        window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
            wc.lpszClassName,L"HQCDD Performance",WS_POPUP,0,0,370,330,owner,nullptr,module,this);
        if(!window) return false;
        SetLayeredWindowAttributes(window,0,225,LWA_ALPHA);
        if(!font) font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        if(!title_font) title_font=CreateFontW(-23,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        SetTimer(window,1,250,nullptr); return true;
    }
    void refresh() {
        if(!window) return;
        DWORD foreground=0,own=0; GetWindowThreadProcessId(GetForegroundWindow(),&foreground); GetWindowThreadProcessId(owner,&own);
        const bool visible=active && !blocked && IsWindowVisible(owner) && !IsIconic(owner) && foreground==own;
        collecting=visible;
        if(!visible) {
            if(was_foreground) { std::lock_guard<std::mutex> lock(mutex); stats.stop(PerformanceOsd::now()); stats.reset(); }
            was_foreground=false; ShowWindow(window,SW_HIDE); return;
        }
        was_foreground=true;
        RECT client{}; GetClientRect(owner,&client); POINT origin{}; ClientToScreen(owner,&origin);
        const int panel_width=std::min(370,int(client.right)),panel_height=std::min(330,int(client.bottom));
        SetWindowPos(window,HWND_TOP,origin.x+std::min(10,std::max(0,int(client.right)-panel_width)),
            origin.y+std::min(10,std::max(0,int(client.bottom)-panel_height)),panel_width,panel_height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        const double t=PerformanceOsd::now();
        if(t-last_system>=1000) {
            FILETIME created{},exited{},kernel{},user{};
            if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) {
                const ULONGLONG total=(ULONGLONG(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+(ULONGLONG(user.dwHighDateTime)<<32)+user.dwLowDateTime;
                if(last_system) cpu_percent=double(total-last_cpu)/10000/(t-last_system)*100;
                last_cpu=total;
            }
            PROCESS_MEMORY_COUNTERS memory{}; memory.cb=sizeof(memory);
            if(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))) ram_mb=memory.WorkingSetSize/1048576.0;
            last_system=t;
        }
        InvalidateRect(window,nullptr,FALSE);
    }
    void paint(HDC target) {
        RECT r{}; GetClientRect(window,&r);
        auto dc=CreateCompatibleDC(target); auto bitmap=CreateCompatibleBitmap(target,std::max(1L,r.right),std::max(1L,r.bottom));
        auto previous=SelectObject(dc,bitmap); auto background=CreateSolidBrush(RGB(12,17,22)); FillRect(dc,&r,background); DeleteObject(background);
        SetBkMode(dc,TRANSPARENT);
        auto text=[&](int y,const wchar_t* value,COLORREF color=RGB(220,232,236),bool title=false) {
            auto old=SelectObject(dc,title?title_font:font); SetTextColor(dc,color); TextOutW(dc,12,y,value,int(wcslen(value))); SelectObject(dc,old);
        };
        const double t=PerformanceOsd::now();
        // Copy bounded data under the lock; sorting and GDI never block the render thread.
        OsdStats snapshot;
        std::vector<double> input_samples;
        bool display_gpu=false,display_vsync=false;
        int display_scaling=0,display_width=0,display_height=0;
        { std::lock_guard<std::mutex> lock(mutex); snapshot=stats;
          if(t-last_wait<=5000) input_samples.assign(waits.begin(),waits.begin()+wait_count);
          display_gpu=gpu; display_vsync=vsync; display_scaling=scaling; display_width=width; display_height=height; }
        const auto& display=snapshot;
        const auto recent=display.recent(t); std::vector<double> intervals,cpu;
        for(const auto& s:recent) { if(s.interval>0) intervals.push_back(s.interval); cpu.push_back(s.cpu); }
        const auto gap=timing_summary(intervals),output=timing_summary(cpu);
        const auto input=timing_summary(std::move(input_samples));
        wchar_t line[160]{};
        text(10,L"HQCDD  |  LIVE PERFORMANCE",RGB(85,216,171));
        swprintf_s(line,L"%6.1f FPS  [output calls]",display.fps(t)); text(32,line,RGB(255,204,106),true);
        const wchar_t* filters[]={L"Nearest",L"Bilinear",L"Sharp Bilinear",L"Integer"};
        swprintf_s(line,L"%s / %s / VSync %s",display_gpu?L"D3D11":L"GDI",filters[std::clamp(display_scaling,0,3)],display_vsync?L"ON":L"OFF"); text(64,line);
        swprintf_s(line,L"%dx%d | CPU %5.1f%% | RAM %.0f MB",display_width,display_height,cpu_percent,ram_mb); text(84,line);
        if(gap.count) swprintf_s(line,L"Frame ms  p95 %6.2f  p99 %6.2f",gap.p95,gap.p99); else wcscpy_s(line,L"Frame ms  p95    N/A  p99    N/A"); text(109,line);
        swprintf_s(line,L"Output CPU p95 %5.2f | max %5.2f ms",output.p95,output.maximum); text(129,line);
        if(input.count) swprintf_s(line,L"Input lock p95 %7.3f ms",input.p95); else wcscpy_s(line,L"Input lock p95     N/A"); text(149,line);
        text(173,L"FRAME INTERVAL   0 - 100 ms",RGB(136,160,175));
        auto pen=CreatePen(PS_SOLID,1,RGB(85,216,171)); auto old_pen=SelectObject(dc,pen);
        for(size_t i=0;i<intervals.size();++i) {
            const int x=12+int(i*340/std::max(size_t(1),intervals.size()-1));
            const int y=230-int(std::min(100.0,intervals[i])*.35);
            if(i) LineTo(dc,x,y); else MoveToEx(dc,x,y,nullptr);
        }
        SelectObject(dc,old_pen); DeleteObject(pen);
        if(display.running || display.finished) {
            const auto bench=timing_summary(std::move(snapshot.benchmark_intervals));
            const double seconds=((display.running?t:display.benchmark_end)-display.benchmark_start)/1000;
            swprintf_s(line,L"BENCH %s %5.1fs | avg %5.1f FPS",display.running?L"REC":L"END",seconds,display.benchmark_fps(t)); text(242,line,RGB(255,204,106));
            if(bench.count>=100) swprintf_s(line,L"p99 %6.2f ms | max %6.2f ms%s",bench.p99,bench.maximum,display.limited?L" *limit":L"");
            else swprintf_s(line,L"%zu intervals | p99 needs 100",bench.count);
            text(262,line);
        } else text(248,L"Ctrl+Alt+B  start benchmark",RGB(136,160,175));
        text(288,L"Ctrl+Alt+F hide | CPU: 1 core=100%",RGB(136,160,175));
        text(307,L"Display FPS / photon latency: N/A",RGB(136,160,175));
        BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);
        SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
    }
    ~Impl() { if(window) DestroyWindow(window); if(font) DeleteObject(font); if(title_font) DeleteObject(title_font); if(module) UnregisterClassW(L"HQCDD.PerformanceOSD",module); }
};
PerformanceOsd::PerformanceOsd():impl(std::make_unique<Impl>()) {}
PerformanceOsd::~PerformanceOsd()=default;
bool PerformanceOsd::enabled() const { return impl->active.load(); }
void PerformanceOsd::attach(HWND owner,HINSTANCE module) { impl->owner=owner; impl->module=module; }
void PerformanceOsd::close() { impl->active=false; impl->collecting=false; if(impl->window) DestroyWindow(impl->window); }
void PerformanceOsd::toggle() {
    if(!impl->owner) return;
    if(impl->active) { impl->active=false; std::lock_guard<std::mutex> lock(impl->mutex); impl->stats.stop(now()); }
    else if(impl->create()) { std::lock_guard<std::mutex> lock(impl->mutex); impl->stats.reset(); impl->wait_count=impl->wait_next=0; impl->active=true; }
    impl->refresh();
}
void PerformanceOsd::benchmark() {
    if(!enabled()) toggle();
    if(!enabled() || !impl->collecting) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(impl->stats.running) impl->stats.stop(now()); else impl->stats.start(now());
}
void PerformanceOsd::suspend(bool value) { impl->blocked=value; impl->refresh(); }
void PerformanceOsd::reset() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->stats.stop(now()); impl->stats.finished=false; impl->stats.reset();
    impl->wait_count=impl->wait_next=0;
}
void PerformanceOsd::frame(double start,bool gpu,int scaling,bool vsync,int width,int height) {
    if(!enabled() || !impl->collecting || start<=0) return;
    const auto t=now(); std::lock_guard<std::mutex> lock(impl->mutex);
    impl->stats.frame(t,t-start); impl->gpu=gpu; impl->scaling=scaling; impl->vsync=vsync; impl->width=width; impl->height=height;
}
void PerformanceOsd::input_wait(double milliseconds) {
    if(!enabled() || !impl->collecting) return;
    std::lock_guard<std::mutex> lock(impl->mutex); impl->waits[impl->wait_next]=milliseconds;
    impl->last_wait=now();
    impl->wait_next=(impl->wait_next+1)%impl->waits.size(); impl->wait_count=std::min(impl->wait_count+1,impl->waits.size());
}
}
