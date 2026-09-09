#pragma once
#include <algorithm>
#include <cstdint>

namespace hq {
// One transform for the framebuffer, native controls and mouse input.
struct Viewport {
    int x=0, y=0, width=1, height=1, game_width=1, game_height=1;
    static Viewport fit(int client_w, int client_h, int game_w, int game_h) {
        client_w=std::max(1,client_w); client_h=std::max(1,client_h);
        game_w=std::max(1,game_w); game_h=std::max(1,game_h);
        int w=client_w, h=client_h;
        if (int64_t(client_w)*game_h > int64_t(client_h)*game_w)
            w=std::max(1,int(int64_t(client_h)*game_w/game_h));
        else h=std::max(1,int(int64_t(client_w)*game_h/game_w));
        return {(client_w-w)/2,(client_h-h)/2,w,h,game_w,game_h};
    }
    int map_x(int logical) const { return x+int(int64_t(logical)*width/game_width); }
    int map_y(int logical) const { return y+int(int64_t(logical)*height/game_height); }
    int unmap_x(int client) const { return int((int64_t(client)-x)*game_width/width); }
    int unmap_y(int client) const { return int((int64_t(client)-y)*game_height/height); }
};
}
