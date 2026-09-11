// Test-only entry point: compile the production coordinate transform as x86.
#include "../src/viewport.h"
extern "C" __declspec(dllexport) void __cdecl ViewportProbe(const int* in, int* out) {
    auto v = hq::Viewport::fit(in[0], in[1], in[2], in[3], in[4] != 0);
    out[0]=v.x; out[1]=v.y; out[2]=v.width; out[3]=v.height;
    out[4]=v.map_x(in[5]); out[5]=v.map_y(in[6]);
    out[6]=v.unmap_x(in[7]); out[7]=v.unmap_y(in[8]);
    out[8]=v.game_x(in[7]); out[9]=v.game_y(in[8]);
}
