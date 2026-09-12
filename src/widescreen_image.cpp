#include "widescreen_runtime.h"
#include "widescreen_recipe.h"
#include <windows.h>
#include <array>
#include <cstring>

namespace hq {
WideRuntimeResult apply_widescreen_image(uint8_t* base,bool wanted,bool syw_viewport) {
    using namespace wide_recipe;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x1000)
        return {false,false,"invalid DOS header",0,WideFailure::structure};
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.ImageBase!=0x400000 ||
       nt->OptionalHeader.SizeOfImage<compatibility.sections[3].address || nt->OptionalHeader.SizeOfImage>64*1024*1024)
        return {false,false,"unsupported mapped image",0,WideFailure::structure};
    // Read and compare every site before allocating memory or writing any patch.
    for(const auto& patch:patches) {
        const size_t rva=patch.address-0x400000;
        if(!patch.size || patch.size>16 || rva>=image_size || patch.size>image_size-rva ||
           std::memcmp(base+rva,patch.before,patch.size)!=0)
            return {false,false,"patch preimage conflict",0,WideFailure::conflict};
    }
    if(!wanted) return {true,false,"original 4:3; no memory patches"};
    // A supported unmodified image can still select 4:3 to clear a rejected
    // saved request. Do not disable both buttons because another plugin owns
    // viewport expansion; no HQCDD patches or allocations occur in this case.
    if(syw_viewport)
        return {true,false,"SYW2X viewport expansion blocks 16:9; 4:3 remains selectable",0,WideFailure::syw2x};
    // Let Windows choose a free address, then relocate the cache operands and
    // both directions of relative jumps between game code and ASI-owned thunks.
    auto memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,
        allocation_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!memory) return {false,false,"widescreen memory allocation failed",0,WideFailure::memory};
    const size_t code_offset=code_address-allocation_base;
    static_assert(code_address>=allocation_base && code_offset+sizeof(code)<=allocation_size);
    constexpr size_t count=sizeof(patches)/sizeof(patches[0]);
    std::array<Patch,count> relocated{};
    std::memcpy(relocated.data(),patches,sizeof(patches));
    const uint32_t delta=uint32_t(uintptr_t(memory))-allocation_base;
    for(const auto& relocation:patch_relocations) {
        auto address=relocated[relocation.index].after+relocation.offset;
        uint32_t value=0; std::memcpy(&value,address,4); value+=delta;
        std::memcpy(address,&value,4);
    }
    std::memcpy(memory+code_offset,code,sizeof(code));
    for(const auto& relocation:code_relocations) {
        auto address=memory+code_offset+relocation.offset;
        uint32_t value=0; std::memcpy(&value,address,4);
        if(relocation.direction>0) value+=delta; else value-=delta;
        std::memcpy(address,&value,4);
    }
    DWORD code_old=0;
    if(!VirtualProtect(memory+code_offset,sizeof(code),PAGE_EXECUTE_READ,&code_old)) {
        VirtualFree(memory,0,MEM_RELEASE);
        return {false,false,"widescreen code protection failed",0,WideFailure::memory};
    }
    std::array<DWORD,count> protections{};
    size_t secured=0;
    for(const auto& patch:patches) {
        if(!VirtualProtect(base+patch.address-0x400000,patch.size,PAGE_EXECUTE_READWRITE,&protections[secured])) break;
        ++secured;
    }
    if(secured==count) {
        for(const auto& patch:relocated)
            std::memcpy(base+patch.address-0x400000,patch.after,patch.size);
        FlushInstructionCache(GetCurrentProcess(),memory+code_offset,sizeof(code));
        FlushInstructionCache(GetCurrentProcess(),base,image_size);
    }
    const bool applied=secured==count;
    while(secured) {
        --secured; DWORD ignored=0;
        VirtualProtect(base+patches[secured].address-0x400000,patches[secured].size,protections[secured],&ignored);
    }
    if(!applied) {
        VirtualFree(memory,0,MEM_RELEASE);
        return {false,false,"game code protection failed; no patches applied",0,WideFailure::memory};
    }
    // Memory belongs to the process lifetime, including after DirectDraw objects
    // are destroyed. Do not free it while the game still contains these jumps.
    return {true,true,"16:9 memory patch active; full-height experimental cache",uintptr_t(memory)};
}

}
