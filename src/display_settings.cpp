#include "display_settings.h"
#include <windows.h>
#include <cwchar>

namespace hq {
DisplaySettings load_display_settings(const wchar_t* path) {
    DisplaySettings settings;
    settings.osd=GetPrivateProfileIntW(L"Diagnostics",L"OSD",0,path)!=0;
    settings.windowed=GetPrivateProfileIntW(L"Display",L"Fullscreen",0,path)==0;
    wchar_t renderer[32]{};
    GetPrivateProfileStringW(L"Display",L"Renderer",L"auto",renderer,32,path);
    settings.renderer_name=renderer;
    settings.gpu=_wcsicmp(renderer,L"gdi")!=0;
    settings.vsync=GetPrivateProfileIntW(L"Display",L"VSync",0,path)!=0;
    const bool legacy_linear=GetPrivateProfileIntW(L"Display",L"LinearFilter",0,path)!=0;
    wchar_t legacy[32]{},filter[32]{};
    GetPrivateProfileStringW(L"Display",L"LinearFilter",L"",legacy,32,path);
    GetPrivateProfileStringW(L"Display",L"Scaling",L"",filter,32,path);
    settings.scaling=parse_scaling(filter,legacy_linear,legacy[0]!=L'\0');
    return settings;
}

bool load_saved_battle_aspect(const wchar_t* path,bool fallback_wide) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(L"Display",L"BattleAspect",fallback_wide?L"16:9":L"4:3",value,32,path);
    return std::wcscmp(value,L"16:9")==0;
}

DisplaySaveResult save_display_settings(const wchar_t* path,const DisplaySettings& settings,
                                       std::optional<bool> aspect) {
    DisplaySaveResult result;
    auto write=[&](const wchar_t* key,const wchar_t* value) {
        const bool saved=WritePrivateProfileStringW(L"Display",key,value,path)!=FALSE;
        result.all_saved=saved && result.all_saved;
        return saved;
    };
    write(L"Fullscreen",settings.windowed?L"0":L"1");
    if(aspect) result.aspect_saved=write(L"BattleAspect",*aspect?L"16:9":L"4:3");
    write(L"Renderer",settings.gpu?L"auto":L"gdi");
    write(L"VSync",settings.vsync?L"1":L"0");
    write(L"LinearFilter",settings.scaling==Bilinear?L"1":L"0");
    write(L"Scaling",scaling_name(settings.scaling));
    return result;
}
}
