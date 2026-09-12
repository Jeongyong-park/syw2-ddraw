#include "battle_aspect.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <vector>
#ifdef HQCDD_ASI
#include "widescreen_runtime.h"
#endif

namespace hq {
bool BattleAspect::requested(const wchar_t* value) { return std::wcscmp(value,L"16:9")==0; }

BattleAspect BattleAspect::initialize(const wchar_t* ini) {
    auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if(uintptr_t(base)!=0x400000) return {};
    auto result=initialize_image(base,ini);
    if(result.available) return result;
#ifdef HQCDD_ASI
    const auto runtime=initialize_widescreen(ini);
    return {runtime.available,runtime.wide,runtime.reason};
#else
    return result;
#endif
}

BattleAspect BattleAspect::initialize_image(uint8_t* base,const wchar_t* ini) {
    BattleAspect result;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return result;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE) return result;
    auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s) {
        const auto& section=sections[s];
        if(std::memcmp(section.Name,".hqcode",8)!=0 || section.Misc.VirtualSize<16) continue;
        auto footer=base+section.VirtualAddress+section.Misc.VirtualSize-16;
        if(std::memcmp(footer,"HQASPECT",8)!=0) return result;
        uint32_t offset=0,count=0;
        std::memcpy(&offset,footer+8,4); std::memcpy(&count,footer+12,4);
        constexpr uint32_t record_size=40;
        if(!count || count>256 || offset>section.Misc.VirtualSize-16 ||
           count*record_size!=section.Misc.VirtualSize-16-offset) return result;
        struct Patch { uint8_t* address; const uint8_t* before; uint32_t length; DWORD protection; };
        std::vector<Patch> patches;
        for(uint32_t i=0;i<count;++i) {
            const auto p=base+section.VirtualAddress+offset+i*record_size;
            uint32_t address=0,length=0;
            std::memcpy(&address,p,4); std::memcpy(&length,p+4,4);
            if(!length || length>16 || address<0x401000 || address>=0x500000 || length>0x500000-address) return result;
            auto target=base+(address-0x400000);
            if(std::memcmp(target,p+24,length)!=0) return result;
            for(const auto& prior:patches)
                if(target<prior.address+prior.length && prior.address<target+length) return result;
            patches.push_back({target,p+8,length,0});
        }
        result={true,true};
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Display",L"BattleAspect",L"16:9",value,32,ini);
        if(requested(value)) return result;
        // Secure every writable span before changing any bytes. Reverse-order
        // protection restoration also handles multiple patches on one page.
        size_t secured=0;
        for(auto& patch:patches) {
            if(!VirtualProtect(patch.address,patch.length,PAGE_EXECUTE_READWRITE,&patch.protection)) break;
            ++secured;
        }
        if(secured==patches.size()) {
            for(const auto& patch:patches) std::memcpy(patch.address,patch.before,patch.length);
            FlushInstructionCache(GetCurrentProcess(),base,nt->OptionalHeader.SizeOfImage);
            result.wide=false;
        } else result.available=false;
        while(secured) {
            const auto& patch=patches[--secured]; DWORD ignored=0;
            VirtualProtect(patch.address,patch.length,patch.protection,&ignored);
        }
        return result;
    }
    return result;
}
}
