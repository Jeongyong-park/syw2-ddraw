#include "frame_change.h"
#include <cstdio>
#include <cstdlib>
void check(bool ok) { if(!ok) std::abort(); }
int main() {
    hq::FrameChange probe; hq::Palette pal{}; hq::Image image(8,8,8);
    const hq::Rect r{2,2,6,6};
    check(probe.sample(image,pal,r).baseline);
    auto s=probe.sample(image,pal,r); check(s.compared==4 && s.changed==0);
    image.write(0,0,1); check(probe.sample(image,pal,r).changed==0);
    image.write(2,2,1); check(probe.sample(image,pal,r).changed==0); // Same RGB.
    pal[1]=0xffffff; check(probe.sample(image,pal,r).changed==1);
    check(probe.sample(image,pal,r).changed==0);
    probe.reset(); check(probe.sample(image,pal,r).baseline);
    check(probe.sample(image,pal,{0,0,9,9}).compared==0);
    check(probe.sample(image,pal,r).baseline);
    hq::Image rgb(8,8,32); check(probe.sample(rgb,pal,r).baseline);
    rgb.write(2,2,0xff000000); check(probe.sample(rgb,pal,r).changed==0);
    rgb.write(4,4,255); check(probe.sample(rgb,pal,r).changed==1);
    hq::Image rgb16(8,8,16); check(probe.sample(rgb16,pal,r).baseline);
    rgb16.write(2,2,0xf800); check(probe.sample(rgb16,pal,r).changed==1);
    puts("PASS: sampled RGB changes, ROI exclusion, palette, formats and baseline resets");
}
