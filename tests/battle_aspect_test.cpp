#include "../src/battle_aspect.h"
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int main() {
    wchar_t folder[MAX_PATH]{},ini[MAX_PATH]{};
    GetTempPathW(MAX_PATH,folder);
    if(!GetTempFileNameW(folder,L"asp",0,ini)) return 1;
    auto base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x110000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    try {
        CHECK(base);
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base); dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x80;
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+0x80); nt->Signature=IMAGE_NT_SIGNATURE;
        nt->FileHeader.NumberOfSections=1; nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER);
        nt->OptionalHeader.SizeOfImage=0x110000;
        auto section=IMAGE_FIRST_SECTION(nt); std::memcpy(section->Name,".hqcode",8);
        section->VirtualAddress=0x100000; section->Misc.VirtualSize=96;
        auto table=base+0x100000;
        uint32_t first=0x401100,second=0x401108,length=1,count=2,offset=0;
        std::memcpy(table,&first,4); std::memcpy(table+4,&length,4); table[8]=3; table[24]=4;
        std::memcpy(table+40,&second,4); std::memcpy(table+44,&length,4); table[48]=8; table[64]=16;
        std::memcpy(table+80,"HQASPECT",8); std::memcpy(table+88,&offset,4); std::memcpy(table+92,&count,4);
        base[0x1100]=4; base[0x1108]=16;
        CHECK(WritePrivateProfileStringW(L"Display",L"BattleAspect",L"16:9",ini));
        auto state=hq::BattleAspect::initialize_image(base,ini);
        CHECK(state.available && state.wide && base[0x1100]==4 && base[0x1108]==16);
        CHECK(WritePrivateProfileStringW(L"Display",L"BattleAspect",L"4:3",ini));
        // A mismatch in the last patch must not partially restore the first one.
        base[0x1108]=99;
        CHECK(!hq::BattleAspect::initialize_image(base,ini).available && base[0x1100]==4);
        base[0x1108]=16;
        DWORD old=0; CHECK(VirtualProtect(base+0x1000,0x1000,PAGE_EXECUTE_READ,&old));
        state=hq::BattleAspect::initialize_image(base,ini);
        CHECK(state.available && !state.wide && base[0x1100]==3 && base[0x1108]==8);
        MEMORY_BASIC_INFORMATION region{}; CHECK(VirtualQuery(base+0x1100,&region,sizeof(region)));
        CHECK(region.Protect==PAGE_EXECUTE_READ);
        CHECK(!hq::BattleAspect::requested(L"invalid") && hq::BattleAspect::requested(L"16:9"));
        VirtualFree(base,0,MEM_RELEASE); DeleteFileW(ini);
        puts("PASS: aspect startup selection, complete preimage validation and shared-page protection restoration");
    } catch(const std::exception& e) {
        if(base) VirtualFree(base,0,MEM_RELEASE); DeleteFileW(ini);
        fprintf(stderr,"FAIL: %s\n",e.what()); return 1;
    }
}
