#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>

namespace hq::perf {
// Explicit opt-in through the child process environment. No per-frame file I/O.
struct Event { const char* name; LONGLONG start,end; DWORD thread; long a,b; };
class Recorder {
    static constexpr size_t capacity=262144;
    std::unique_ptr<Event[]> events;
    size_t count=0, dropped=0;
    std::mutex mutex;
    wchar_t path[32768]{};
    LONGLONG frequency=1;
public:
    std::atomic<bool> enabled{false};
    Recorder() {
        const auto n=GetEnvironmentVariableW(L"HQCDD_PERF_FILE",path,32768);
        if(!n || n>=32768) return;
        LARGE_INTEGER f{}; if(!QueryPerformanceFrequency(&f)) return;
        frequency=f.QuadPart;
        events.reset(new(std::nothrow) Event[capacity]);
        enabled=bool(events);
    }
    static LONGLONG now() { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart; }
    void add(const Event& e) {
        std::lock_guard<std::mutex> lock(mutex);
        if(!enabled) return;
        if(count<capacity) events[count++]=e; else ++dropped;
    }
    // Called on game HWND teardown, outside DllMain. Stop accepting events first.
    void finish() {
        std::lock_guard<std::mutex> lock(mutex);
        if(!enabled.exchange(false)) return;
        const auto finished=now();
        FILE* f=nullptr;
        if(_wfopen_s(&f,path,L"wb")!=0) return;
        fprintf(f,"event,start_qpc,end_qpc,frequency,thread,a,b\n");
        for(size_t i=0;i<count;++i) {
            const auto& e=events[i];
            fprintf(f,"%s,%lld,%lld,%lld,%lu,%ld,%ld\n",e.name,e.start,e.end,frequency,e.thread,e.a,e.b);
        }
        fprintf(f,"trace_dropped,%lld,%lld,%lld,0,%zu,0\n",finished,finished,frequency,dropped);
        fclose(f);
    }
};
inline Recorder& recorder() { static Recorder r; return r; }
struct Scope {
    Recorder& r;
    Event e{};
    explicit Scope(const char* name,long a=0,long b=0):r(recorder()) {
        if(name && r.enabled) e={name,Recorder::now(),0,GetCurrentThreadId(),a,b};
    }
    ~Scope() { if(e.name) { e.end=Recorder::now(); r.add(e); } }
};
inline void mark(const char* name,long a=0,long b=0) { Scope scope(name,a,b); }
}
