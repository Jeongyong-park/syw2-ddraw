#include "../src/widescreen.h"
#include <cstdio>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
    hq::WideTerrain cache;
    CHECK(cache.hud_x==134 && cache.columns==17 && cache.rows==16);
    for(size_t i=0;i<cache.pixels.size();++i) cache.pixels[i]=uint8_t(i%251+1);
    const auto original=cache.pixels;
    const int pitch=1080;
    std::vector<uint8_t> output(pitch*512+16,0xcd);
    CHECK(!cache.copy_visible(output.data(),100,pitch,512));
    CHECK(std::all_of(output.begin(),output.end(),[](auto b){return b==0xcd;}));
    CHECK(!cache.copy_visible(output.data(),output.size(),1067,512));
    CHECK(cache.copy_visible(output.data(),output.size(),pitch,512));
    for(int y=0;y<512;++y) {
        for(int x=0;x<1068;++x) CHECK(output[y*pitch+x]==original[y*1088+x]);
        for(int x=1068;x<pitch;++x) CHECK(output[y*pitch+x]==0xcd);
    }
    for(size_t i=pitch*512;i<output.size();++i) CHECK(output[i]==0xcd);
    std::fill(cache.dirty.begin(),cache.dirty.end(),uint8_t(0));
    cache.mark(1024,480,1088,512);
    CHECK(cache.dirty[16*16+15]==1);
    CHECK(std::count(cache.dirty.begin(),cache.dirty.end(),1)==1);
    cache.mark(-100,-100,1,1); CHECK(cache.dirty[0]==1);
    for(int dy:{-32,0,32}) for(int dx:{-64,0,64}) {
        cache.pixels=original; cache.scroll(dx,dy);
        for(int y=0;y<512;++y) for(int x=0;x<1088;++x) {
            const int sx=x-dx,sy=y-dy;
            const auto expected=sx>=0 && sx<1088 && sy>=0 && sy<512?original[sy*1088+sx]:0;
            CHECK(cache.pixels[y*1088+x]==expected);
        }
    }
    cache.scroll(1088,0);
    CHECK(std::all_of(cache.pixels.begin(),cache.pixels.end(),[](auto b){return b==0;}));
    puts("PASS: wide terrain pitch, edge blocks, guarded copy and all scroll directions");
}
