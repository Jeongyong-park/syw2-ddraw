#include "../src/osd_stats.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    CHECK(hq::timing_summary({}).count==0);
    std::vector<double> values;
    for(int i=100;i>=1;--i) values.push_back(i);
    const auto summary=hq::timing_summary(values);
    CHECK(summary.p95==95 && summary.p99==99 && summary.maximum==100 && summary.mean==50.5);
    hq::OsdStats stats;
    CHECK(stats.fps(0)==0);
    for(int i=0;i<=100;++i) stats.frame(i*50,1);
    CHECK(stats.fps(5000)==20);
    CHECK(stats.fps(6100)==0);
    stats.start(7000);
    stats.frame(7050,1); stats.frame(7100,1);
    CHECK(stats.benchmark_intervals.size()==1 && stats.benchmark_intervals[0]==50);
    stats.stop(7200);
    CHECK(stats.benchmark_fps(9000)==10);
    stats.reset(); CHECK(stats.fps(9000)==0 && stats.finished);
    stats.start(0);
    for(int i=0;i<=120001;++i) stats.frame(i,1);
    CHECK(!stats.running && stats.limited && stats.benchmark_intervals.size()==120000);
    CHECK(stats.recent(120001).size()==240);
    std::puts("OSD statistics passed");
}
