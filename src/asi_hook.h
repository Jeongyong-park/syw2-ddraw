#pragma once
#include <windows.h>
#include <cstring>

namespace hq {
// Only the main executable's named DDRAW!DirectDrawCreateEx import is eligible.
// No process-wide API patch, delay import, ordinal import or EXE file mutation.
inline void** draw_import(HMODULE executable) {
    auto base=reinterpret_cast<unsigned char*>(executable);
    if(!base) return nullptr;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x100000) return nullptr;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC) return nullptr;
    const size_t size=nt->OptionalHeader.SizeOfImage;
    auto fits=[&](size_t rva,size_t bytes) { return rva<size && bytes<=size-rva; };
    auto string=[&](DWORD rva)->const char* {
        if(!fits(rva,1) || !std::memchr(base+rva,0,size-rva)) return nullptr;
        return reinterpret_cast<const char*>(base+rva);
    };
    const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress || !fits(dir.VirtualAddress,dir.Size)) return nullptr;
    void** found=nullptr;
    for(size_t offset=0;offset+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=dir.Size;offset+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto d=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress+offset);
        if(!d->Name) return found;
        const auto name=string(d->Name);
        if(!name) return nullptr;
        if(_stricmp(name,"ddraw.dll")!=0) continue;
        if(!d->OriginalFirstThunk || !d->FirstThunk) return nullptr;
        for(size_t i=0;;i+=sizeof(IMAGE_THUNK_DATA32)) {
            const size_t names=size_t(d->OriginalFirstThunk)+i,slot=size_t(d->FirstThunk)+i;
            if(!fits(names,4) || !fits(slot,4) || slot%alignof(void*)) return nullptr;
            const auto rva=*reinterpret_cast<const DWORD*>(base+names);
            if(!rva) break;
            if(IMAGE_SNAP_BY_ORDINAL32(rva) || !fits(rva,3)) return nullptr;
            const auto function=string(rva+2);
            if(!function) return nullptr;
            if(std::strcmp(function,"DirectDrawCreateEx")!=0) return nullptr;
            if(found) return nullptr;
            found=reinterpret_cast<void**>(base+slot);
        }
    }
    return nullptr;
}
}
