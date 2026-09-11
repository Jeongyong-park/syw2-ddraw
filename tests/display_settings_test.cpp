#include "../src/display_settings.h"
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)

int main() {
    wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};
    try {
        CHECK(GetTempPathW(MAX_PATH,folder));
        CHECK(GetTempFileNameW(folder,L"hqs",0,path));
        auto put=[&](const wchar_t* key,const wchar_t* value) {
            CHECK(WritePrivateProfileStringW(L"Display",key,value,path));
        };
        auto state=hq::load_display_settings(path);
        CHECK(state.windowed && state.gpu && !state.vsync && !state.osd);
        CHECK(state.scaling==hq::SharpBilinear);
        CHECK(!hq::load_saved_battle_aspect(path,false));
        CHECK(hq::load_saved_battle_aspect(path,true));
        put(L"LinearFilter",L"0"); CHECK(hq::load_display_settings(path).scaling==hq::Nearest);
        put(L"LinearFilter",L"1"); CHECK(hq::load_display_settings(path).scaling==hq::Bilinear);
        put(L"Scaling",L"SHARP-BILINEAR"); CHECK(hq::load_display_settings(path).scaling==hq::SharpBilinear);
        put(L"Scaling",L"unknown"); CHECK(hq::load_display_settings(path).scaling==hq::Nearest);
        put(L"Renderer",L"GDI"); CHECK(!hq::load_display_settings(path).gpu);
        put(L"Renderer",L"unknown"); CHECK(hq::load_display_settings(path).gpu);
        put(L"BattleAspect",L"16:9"); CHECK(hq::load_saved_battle_aspect(path,false));
        CHECK(WritePrivateProfileStringW(L"Diagnostics",L"OSD",L"1",path));
        put(L"ExternalKey",L"preserved");
        state.windowed=false; state.gpu=true; state.vsync=true; state.scaling=hq::Integer;
        auto saved=hq::save_display_settings(path,state);
        CHECK(saved.all_saved && !saved.aspect_saved);
        CHECK(hq::load_saved_battle_aspect(path,false)); // Omitted aspect is preserved.
        auto loaded=hq::load_display_settings(path);
        CHECK(!loaded.windowed && loaded.gpu && loaded.vsync && loaded.osd && loaded.scaling==hq::Integer);
        CHECK(GetPrivateProfileIntW(L"Display",L"LinearFilter",99,path)==0);
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Display",L"ExternalKey",L"",value,32,path);
        CHECK(std::wstring(value)==L"preserved");
        saved=hq::save_display_settings(path,state,false);
        CHECK(saved.all_saved && saved.aspect_saved);
        CHECK(!hq::load_saved_battle_aspect(path,true)); // optional(false) must still write 4:3.
        state.gpu=false; state.scaling=hq::Bilinear;
        saved=hq::save_display_settings(path,state,true);
        CHECK(saved.all_saved && saved.aspect_saved);
        loaded=hq::load_display_settings(path);
        CHECK(!loaded.gpu && loaded.scaling==hq::Bilinear);
        CHECK(GetPrivateProfileIntW(L"Display",L"LinearFilter",99,path)==1);
        // A child of an existing file cannot be created as an INI.
        const auto invalid=std::wstring(path)+L"\\settings.ini";
        saved=hq::save_display_settings(invalid.c_str(),state,false);
        CHECK(!saved.all_saved && !saved.aspect_saved);
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,path);
        CHECK(DeleteFileW(path));
        std::puts("PASS: display defaults, legacy keys, round trip, preservation and write failure");
    } catch(const std::exception& error) {
        if(*path) { WritePrivateProfileStringW(nullptr,nullptr,nullptr,path); DeleteFileW(path); }
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
