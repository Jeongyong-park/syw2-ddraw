#include "widescreen_runtime.h"
#include "widescreen_recipe.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace hq {
namespace {
bool matching_file() {
    wchar_t path[32768]{};
    const auto length=GetModuleFileNameW(nullptr,path,32768);
    if(!length || length>=32768) return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD object_size=0,received=0;
    bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(okay) okay=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&received,0)>=0;
    std::vector<uint8_t> object(okay?object_size:0);
    if(okay) okay=BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0)>=0;
    std::array<uint8_t,65536> buffer{};
    while(okay) {
        DWORD count=0;
        if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&count,nullptr)) { okay=false; break; }
        if(!count) break;
        okay=BCryptHashData(hash,buffer.data(),count,0)>=0;
    }
    std::array<uint8_t,32> digest{};
    if(okay) okay=BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)>=0 &&
        std::memcmp(digest.data(),wide_recipe::sha256,digest.size())==0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);
    return okay;
}
}

WideRuntimeResult initialize_widescreen(const wchar_t* ini) {
    auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if(uintptr_t(base)!=0x400000 || !matching_file()) return {};
    wchar_t value[32]{};
    GetPrivateProfileStringW(L"Display",L"BattleAspect",L"4:3",value,32,ini);
    const bool wanted=std::wcscmp(value,L"16:9")==0;
    // The old SYW2X viewport hook has not been reconciled with this recipe yet.
    // Preserve its configuration instead of silently changing another plugin.
    std::wstring syw_path=ini;
    const auto slash=syw_path.find_last_of(L"\\/");
    syw_path=(slash==std::wstring::npos?L"":syw_path.substr(0,slash+1))+L"syw2x.ini";
    const bool syw_viewport=GetPrivateProfileIntW(L"ViewPortControl",L"ViewPortPlusOn",0,syw_path.c_str())!=0;
    return apply_widescreen_image(base,wanted,syw_viewport);
}
}
