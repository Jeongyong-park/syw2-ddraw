#pragma once
#include <windows.h>
#include <memory>
namespace hq {
class PerformanceOsd {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    PerformanceOsd();
    ~PerformanceOsd();
    bool enabled() const;
    void attach(HWND owner,HINSTANCE module);
    void close();
    void toggle();
    void benchmark();
    void suspend(bool value);
    void reset();
    void frame(double start,bool gpu,int scaling,bool vsync,int width,int height);
    void input_wait(double milliseconds);
    static double now();
};
}
