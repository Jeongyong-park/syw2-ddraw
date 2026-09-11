#include <windows.h>
#include "../src/widescreen_runtime.h"
#include "../src/widescreen_recipe.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int main() {
    using namespace hq::wide_recipe;
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    void* occupied=nullptr;
    uintptr_t owned=0;
    try {
        CHECK(image);
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image); dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x80;
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x80); nt->Signature=IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386; nt->FileHeader.TimeDateStamp=timestamp;
        nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC; nt->OptionalHeader.ImageBase=0x400000;
        nt->OptionalHeader.SizeOfImage=image_size;
        for(const auto& p:patches) std::memcpy(image+p.address-0x400000,p.before,p.size);
        auto unchanged=[&] { for(const auto& p:patches) CHECK(!std::memcmp(image+p.address-0x400000,p.before,p.size)); };
        auto state=hq::apply_widescreen_image(image,false);
        CHECK(state.available && !state.wide); unchanged();
        state=hq::apply_widescreen_image(image,true,true);
        CHECK(state.available && !state.wide && !state.allocation); unchanged();
        state=hq::apply_widescreen_image(image,false,true);
        CHECK(state.available && !state.wide && !state.allocation); unchanged();
        constexpr size_t count=sizeof(patches)/sizeof(patches[0]);
        const auto& last=patches[count-1];
        image[last.address-0x400000]^=1;
        CHECK(!hq::apply_widescreen_image(image,true).available);
        CHECK(!hq::apply_widescreen_image(image,true,true).available);
        image[last.address-0x400000]^=1; unchanged();
        occupied=VirtualAlloc(reinterpret_cast<void*>(allocation_base),allocation_size,MEM_RESERVE,PAGE_NOACCESS);
        CHECK(occupied || GetLastError()==ERROR_INVALID_ADDRESS);
        DWORD old=0;
        CHECK(VirtualProtect(image,image_size,PAGE_EXECUTE_READ,&old));
        state=hq::apply_widescreen_image(image,true);
        CHECK(state.available && state.wide);
        owned=state.allocation;
        CHECK(owned && owned!=allocation_base);
        CHECK(image[0x41b4db-0x400000]==4);
        uint32_t pointer=0; std::memcpy(&pointer,image+0x41bf40-0x400000,4);
        CHECK(pointer==owned+0x1000); // original copy function's cache source
        int32_t displacement=0; std::memcpy(&displacement,image+0x41c604-0x400000,4);
        CHECK(uint32_t(0x41c608+displacement)==owned+(code_address-allocation_base));
        for(const auto& p:patches) {
            MEMORY_BASIC_INFORMATION memory{};
            CHECK(VirtualQuery(image+p.address-0x400000,&memory,sizeof(memory)));
            CHECK(memory.Protect==PAGE_EXECUTE_READ);
        }
        MEMORY_BASIC_INFORMATION memory{};
        CHECK(VirtualQuery(reinterpret_cast<void*>(owned+code_address-allocation_base),&memory,sizeof(memory)));
        CHECK(memory.Protect==PAGE_EXECUTE_READ);
        CHECK(VirtualQuery(reinterpret_cast<void*>(owned),&memory,sizeof(memory)));
        CHECK(memory.Protect==PAGE_READWRITE);
        CHECK(!hq::apply_widescreen_image(image,true).available); // no double patch
        VirtualFree(reinterpret_cast<void*>(owned),0,MEM_RELEASE); owned=0;
        if(occupied) VirtualFree(occupied,0,MEM_RELEASE); occupied=nullptr;
        VirtualFree(image,0,MEM_RELEASE);
        printf("PASS: %zu original-image patches, 4:3 unchanged, mismatch rollback, occupied-address relocation, RX code and protection restoration\n",count);
    } catch(const std::exception& e) {
        if(occupied) VirtualFree(occupied,0,MEM_RELEASE);
        if(owned) VirtualFree(reinterpret_cast<void*>(owned),0,MEM_RELEASE);
        if(image) VirtualFree(image,0,MEM_RELEASE);
        fprintf(stderr,"FAIL: %s\n",e.what()); return 1;
    }
}
