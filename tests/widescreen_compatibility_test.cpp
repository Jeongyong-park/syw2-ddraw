#include "../src/widescreen_compatibility.h"
#include "../src/widescreen_recipe.h"
#include "../src/widescreen_runtime.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int wmain(int argc,wchar_t** argv) {
    using namespace hq;
    try {
        if(argc==2 || (argc==3 && wcscmp(argv[2],L"--apply")==0)) {
            std::ifstream file(std::filesystem::path(argv[1]),std::ios::binary);
            CHECK(file.good()); std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),{});
            const auto result=check_widescreen_file(raw.data(),raw.size(),wide_recipe::compatibility);
            std::printf("Compatibility result: %d\n",int(result));
            if(result==WideFailure::none && argc==3) {
                // Private mapping only: never execute the game or alter its file.
                const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
                const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
                auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
                CHECK(image);
                std::memcpy(image,raw.data(),nt->OptionalHeader.SizeOfHeaders);
                const auto sections=IMAGE_FIRST_SECTION(nt);
                for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
                    std::memcpy(image+sections[i].VirtualAddress,raw.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
                const auto applied=apply_widescreen_image(image,true);
                if(applied.allocation) VirtualFree(reinterpret_cast<void*>(applied.allocation),0,MEM_RELEASE);
                VirtualFree(image,0,MEM_RELEASE);
                CHECK(applied.wide && applied.failure==WideFailure::none);
                std::puts("PASS: compatible file applies all 16:9 patches to a private memory image");
            }
            return int(result);
        }
        auto profile=wide_recipe::compatibility;
        std::vector<uint8_t> raw(0x200000);
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(raw.data()); dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x80;
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
        nt->Signature=IMAGE_NT_SIGNATURE; nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;
        nt->FileHeader.NumberOfSections=4; nt->FileHeader.SizeOfOptionalHeader=sizeof(nt->OptionalHeader);
        nt->FileHeader.Characteristics=profile.characteristics;
        auto& opt=nt->OptionalHeader;
        opt.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC; opt.ImageBase=0x400000;
        opt.AddressOfEntryPoint=profile.entry; opt.SectionAlignment=profile.section_alignment;
        opt.FileAlignment=profile.file_alignment; opt.SizeOfHeaders=profile.headers;
        opt.SizeOfImage=wide_recipe::image_size; opt.DllCharacteristics=profile.dll_characteristics;
        opt.Subsystem=profile.subsystem; opt.NumberOfRvaAndSizes=16;
        std::memcpy(opt.DataDirectory,profile.directories,sizeof(profile.directories));
        auto sections=IMAGE_FIRST_SECTION(nt); DWORD offset=profile.headers;
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        CHECK(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
        for(int i=0;i<4;++i) {
            auto& section=sections[i]; auto& expected=profile.sections[i];
            std::memcpy(section.Name,expected.name,8);
            section.VirtualAddress=expected.address; section.Misc.VirtualSize=expected.virtual_size;
            section.Characteristics=expected.characteristics; section.SizeOfRawData=expected.raw_size;
            section.PointerToRawData=offset;
            std::memset(raw.data()+offset,i+1,section.SizeOfRawData);
            CHECK(BCryptHash(algorithm,nullptr,0,raw.data()+offset,section.SizeOfRawData,expected.sha256,32)>=0);
            offset+=section.SizeOfRawData;
        }
        BCryptCloseAlgorithmProvider(algorithm,0);
        const auto check=[&] { return check_widescreen_file(raw.data(),raw.size(),profile); };
        CHECK(check()==WideFailure::none);
        // Metadata and resource edits do not weaken executable/data validation.
        nt->FileHeader.TimeDateStamp^=0x12345678; opt.CheckSum=1234;
        raw[sections[3].PointerToRawData+100]^=1;
        CHECK(check()==WideFailure::none);
        sections[3].Misc.VirtualSize+=0x1000; sections[3].SizeOfRawData+=0x1000;
        opt.DataDirectory[2].Size+=0x1000; opt.SizeOfImage+=0x1000;
        CHECK(check()==WideFailure::none);
        for(int i=0;i<3;++i) {
            raw[sections[i].PointerToRawData+100]^=1;
            CHECK(check()==(i==0?WideFailure::code:WideFailure::data));
            raw[sections[i].PointerToRawData+100]^=1;
        }
        sections[0].VirtualAddress+=0x1000; CHECK(check()==WideFailure::structure); sections[0].VirtualAddress-=0x1000;
        opt.AddressOfEntryPoint++; CHECK(check()==WideFailure::structure); opt.AddressOfEntryPoint--;
        opt.DataDirectory[9]={0x1000,24}; CHECK(check()==WideFailure::structure); opt.DataDirectory[9]={};
        opt.DataDirectory[1].VirtualAddress++; CHECK(check()==WideFailure::structure); opt.DataDirectory[1].VirtualAddress--;
        sections[3].Characteristics|=IMAGE_SCN_MEM_EXECUTE; CHECK(check()==WideFailure::structure);
        sections[3].Characteristics=profile.sections[3].characteristics;
        const auto pointer=sections[3].PointerToRawData;
        sections[3].PointerToRawData=sections[0].PointerToRawData; CHECK(check()==WideFailure::structure);
        sections[3].PointerToRawData=0xfffffff0; CHECK(check()==WideFailure::structure);
        sections[3].PointerToRawData=pointer;
        sections[3].PointerToRawData++; CHECK(check()==WideFailure::structure); sections[3].PointerToRawData--;
        nt->FileHeader.NumberOfSections=5; CHECK(check()==WideFailure::structure); nt->FileHeader.NumberOfSections=4;
        CHECK(check()==WideFailure::none);
        for(size_t length:{size_t(0),size_t(63),size_t(128),size_t(511),size_t(offset-1)})
            CHECK(check_widescreen_file(raw.data(),length,profile)!=WideFailure::none);
        CHECK(check_widescreen_file(nullptr,raw.size(),profile)==WideFailure::structure);
        CHECK(*widescreen_failure_message(WideFailure::code));
        CHECK(!*widescreen_failure_message(WideFailure::none));
        std::puts("PASS: resource edits and growth, code/data changes, section layout, imports/TLS and malformed bounds");
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
