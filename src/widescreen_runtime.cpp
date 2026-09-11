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

WideRuntimeResult apply_widescreen_image(uint8_t* base,bool wanted,bool syw_viewport) {
    using namespace wide_recipe;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x1000) return {};
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.ImageBase!=0x400000 ||
       nt->OptionalHeader.SizeOfImage!=image_size || nt->FileHeader.TimeDateStamp!=timestamp) return {};
    // Read and compare every site before allocating memory or writing any patch.
    for(const auto& patch:patches) {
        const size_t rva=patch.address-0x400000;
        if(!patch.size || patch.size>16 || rva>=image_size || patch.size>image_size-rva ||
           std::memcmp(base+rva,patch.before,patch.size)!=0)
            return {false,false,"patch preimage conflict"};
    }
    if(!wanted) return {true,false,"original 4:3; no memory patches"};
    // A supported unmodified image can still select 4:3 to clear a rejected
    // saved request. Do not disable both buttons because another plugin owns
    // viewport expansion; no HQCDD patches or allocations occur in this case.
    if(syw_viewport)
        return {true,false,"SYW2X viewport expansion blocks 16:9; 4:3 remains selectable"};
    // Let Windows choose a free address, then relocate the cache operands and
    // both directions of relative jumps between game code and ASI-owned thunks.
    auto memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,
        allocation_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!memory) return {false,false,"widescreen memory allocation failed"};
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
        return {false,false,"widescreen code protection failed"};
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
        return {false,false,"game code protection failed; no patches applied"};
    }
    // Memory belongs to the process lifetime, including after DirectDraw objects
    // are destroyed. Do not free it while the game still contains these jumps.
    return {true,true,"16:9 memory patch active; full-height experimental cache",uintptr_t(memory)};
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
