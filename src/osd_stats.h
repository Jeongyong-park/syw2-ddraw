#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace hq {
struct TimingSummary {
    size_t count=0;
    double mean=0,p95=0,p99=0,maximum=0;
};
inline TimingSummary timing_summary(std::vector<double> values) {
    TimingSummary r; r.count=values.size(); if(values.empty()) return r;
    for(double v:values) r.mean+=v;
    r.mean/=values.size(); r.maximum=*std::max_element(values.begin(),values.end());
    const auto p95=size_t(std::ceil(values.size()*.95))-1,p99=size_t(std::ceil(values.size()*.99))-1;
    std::nth_element(values.begin(),values.begin()+p95,values.end()); r.p95=values[p95];
    std::nth_element(values.begin(),values.begin()+p99,values.end()); r.p99=values[p99]; return r;
}
struct OsdSample { double at=0,interval=0,cpu=0; };
class OsdStats {
    std::array<OsdSample,240> ring{};
    size_t next=0,used=0;
    double last=0; bool have_last=false;
public:
    bool running=false, finished=false,limited=false;
    double benchmark_start=0,benchmark_end=0;
    size_t benchmark_frames=0;
    std::vector<double> benchmark_intervals;
    void reset() { next=used=0; have_last=false; }
    void start(double now) {
        running=true; finished=limited=false; benchmark_start=now; benchmark_end=now;
        benchmark_frames=0; benchmark_intervals.clear(); benchmark_intervals.reserve(120000);
    }
    void stop(double now) { if(running) { running=false; finished=true; benchmark_end=now; } }
    void frame(double now,double cpu) {
        const double interval=have_last?now-last:0;
        if(have_last && interval<0) return;
        ring[next]={now,interval,cpu}; next=(next+1)%ring.size(); used=std::min(used+1,ring.size());
        last=now; have_last=true;
        if(running) {
            if(benchmark_frames && interval>0) benchmark_intervals.push_back(interval);
            ++benchmark_frames;
            if(benchmark_intervals.size()>=120000) { limited=true; stop(now); }
        }
    }
    std::vector<OsdSample> recent(double now) const {
        std::vector<OsdSample> result;
        for(size_t i=0;i<used;++i) {
            const auto& s=ring[(next+ring.size()-used+i)%ring.size()];
            if(now-s.at<=5000) result.push_back(s);
        }
        return result;
    }
    double fps(double now) const {
        if(!have_last || now-last>1000) return 0;
        const auto samples=recent(now);
        if(samples.size()<2) return 0;
        const double duration=now-samples.front().at;
        return duration>0?(samples.size()-1)*1000/duration:0;
    }
    double benchmark_fps(double now) const {
        const double duration=(running?now:benchmark_end)-benchmark_start;
        return duration>0?benchmark_frames*1000/duration:0;
    }
};
}
