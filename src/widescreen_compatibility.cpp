#include "widescreen_compatibility.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>

namespace hq {
namespace {
bool hash_matches(const uint8_t* bytes,ULONG size,const uint8_t* expected) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    std::array<uint8_t,32> digest{};
    const bool okay=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(bytes),size,
        digest.data(),ULONG(digest.size()))>=0 && !std::memcmp(digest.data(),expected,digest.size());
    BCryptCloseAlgorithmProvider(algorithm,0);
    return okay;
}
}
WideFailure check_widescreen_file(const uint8_t* bytes,size_t size,const WideProfile& profile) {
    const auto fits=[&](size_t offset,size_t length) { return offset<=size && length<=size-offset; };
    if(!bytes || !fits(0,sizeof(IMAGE_DOS_HEADER))) return WideFailure::structure;
    IMAGE_DOS_HEADER dos{}; std::memcpy(&dos,bytes,sizeof(dos));
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || dos.e_lfanew>4096 ||
       !fits(dos.e_lfanew,sizeof(IMAGE_NT_HEADERS32))) return WideFailure::structure;
    IMAGE_NT_HEADERS32 nt{}; std::memcpy(&nt,bytes+dos.e_lfanew,sizeof(nt));
    const auto& opt=nt.OptionalHeader;
    if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt.FileHeader.NumberOfSections!=4 || nt.FileHeader.SizeOfOptionalHeader!=sizeof(opt) ||
       nt.FileHeader.Characteristics!=profile.characteristics || opt.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
       opt.ImageBase!=0x400000 || opt.AddressOfEntryPoint!=profile.entry ||
       opt.SectionAlignment!=profile.section_alignment || opt.FileAlignment!=profile.file_alignment ||
       opt.SizeOfHeaders!=profile.headers || !fits(0,opt.SizeOfHeaders) ||
       opt.DllCharacteristics!=profile.dll_characteristics || opt.Subsystem!=profile.subsystem ||
       opt.NumberOfRvaAndSizes!=16 || !opt.SizeOfImage || opt.SizeOfImage>64*1024*1024 ||
       !opt.FileAlignment || !opt.SectionAlignment || opt.SizeOfImage%opt.SectionAlignment)
        return WideFailure::structure;
    for(unsigned i=0;i<16;++i) {
        if(i==IMAGE_DIRECTORY_ENTRY_RESOURCE || i==IMAGE_DIRECTORY_ENTRY_SECURITY) continue;
        if(std::memcmp(&opt.DataDirectory[i],&profile.directories[i],sizeof(IMAGE_DATA_DIRECTORY)))
            return WideFailure::structure;
    }
    const auto table=size_t(dos.e_lfanew)+sizeof(nt);
    std::array<IMAGE_SECTION_HEADER,4> sections{};
    if(!fits(table,sizeof(sections)) || table+sizeof(sections)>opt.SizeOfHeaders) return WideFailure::structure;
    std::memcpy(sections.data(),bytes+table,sizeof(sections));
    uint64_t previous_end=opt.SizeOfHeaders;
    for(size_t i=0;i<sections.size();++i) {
        const auto& section=sections[i]; const auto& expected=profile.sections[i];
        const bool resource=i==3;
        const uint64_t end=uint64_t(section.VirtualAddress)+std::max(section.Misc.VirtualSize,section.SizeOfRawData);
        if(std::memcmp(section.Name,expected.name,8) || section.VirtualAddress!=expected.address ||
           section.Characteristics!=expected.characteristics || section.VirtualAddress<previous_end ||
           end>opt.SizeOfImage || section.PointerToRawData<opt.SizeOfHeaders ||
           section.PointerToRawData%opt.FileAlignment || section.SizeOfRawData%opt.FileAlignment ||
           !fits(section.PointerToRawData,section.SizeOfRawData) ||
           (!resource && (section.Misc.VirtualSize!=expected.virtual_size || section.SizeOfRawData!=expected.raw_size)))
            return WideFailure::structure;
        previous_end=end;
        for(size_t j=0;j<i;++j) {
            const auto& other=sections[j];
            if(uint64_t(section.PointerToRawData)<uint64_t(other.PointerToRawData)+other.SizeOfRawData &&
               uint64_t(other.PointerToRawData)<uint64_t(section.PointerToRawData)+section.SizeOfRawData)
                return WideFailure::structure;
        }
        if(!resource && !hash_matches(bytes+section.PointerToRawData,section.SizeOfRawData,expected.sha256))
            return i==0?WideFailure::code:WideFailure::data;
    }
    const auto& resource=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    const auto& section=sections.back();
    if(resource.VirtualAddress!=section.VirtualAddress || resource.Size>section.Misc.VirtualSize ||
       resource.Size>section.SizeOfRawData) return WideFailure::structure;
    const auto& certificate=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    if(certificate.Size && !fits(certificate.VirtualAddress,certificate.Size)) return WideFailure::structure;
    return WideFailure::none;
}
const wchar_t* widescreen_failure_message(WideFailure failure) {
    switch(failure) {
    case WideFailure::file: return L"실행파일을 읽을 수 없어 16:9 호환성을 확인하지 못했습니다.";
    case WideFailure::structure: return L"실행파일의 주소 배치 또는 구조가 지원하는 형식과 다릅니다.";
    case WideFailure::code: return L"게임 실행 코드가 변경되어 16:9를 적용할 수 없습니다.";
    case WideFailure::data: return L"게임 데이터 구조가 변경되어 16:9를 적용할 수 없습니다.";
    case WideFailure::conflict: return L"화면 처리 메모리가 변경되어 16:9 패치가 충돌합니다. 다른 플러그인의 화면 확장 설정을 확인하세요.";
    case WideFailure::syw2x: return L"SYW2X의 뷰포트 확장이 켜져 있습니다. 확장을 끄고 게임을 재실행하면 16:9를 사용할 수 있습니다.";
    case WideFailure::memory: return L"16:9에 필요한 메모리를 준비하지 못했습니다.";
    default: return L"";
    }
}
}
