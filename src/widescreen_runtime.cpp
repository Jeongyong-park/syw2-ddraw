#include "widescreen_runtime.h"
#include "widescreen_recipe.h"
#include <windows.h>
#include <cwchar>
#include <string>
#include <vector>
#include <new>

namespace hq {
namespace {
WideFailure compatible_file() {
    wchar_t path[32768]{};
    const auto length=GetModuleFileNameW(nullptr,path,32768);
    if(!length || length>=32768) return WideFailure::file;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return WideFailure::file;
    struct Close { HANDLE handle; ~Close(){CloseHandle(handle);} } close{file};
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<=0 || size.QuadPart>64*1024*1024) return WideFailure::file;
    std::vector<uint8_t> bytes(size_t(size.QuadPart));
    DWORD received=0;
    if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&received,nullptr) || received!=bytes.size()) return WideFailure::file;
    return check_widescreen_file(bytes.data(),bytes.size(),wide_recipe::compatibility);
}
}

WideRuntimeResult initialize_widescreen(const wchar_t* ini) {
    auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if(uintptr_t(base)!=0x400000) return {false,false,"unsupported image base",0,WideFailure::structure};
    WideFailure failure=WideFailure::none;
    try { failure=compatible_file(); }
    catch(const std::bad_alloc&) { failure=WideFailure::memory; }
    if(failure!=WideFailure::none) {
        const char* reason=failure==WideFailure::code?"executable code section differs":
            failure==WideFailure::data?"executable data section differs":
            failure==WideFailure::file?"executable file read failed":
            failure==WideFailure::memory?"executable validation allocation failed":"executable layout differs";
        return {false,false,reason,0,failure};
    }
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
