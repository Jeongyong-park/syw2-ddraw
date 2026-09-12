#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace hq {
enum class WideFailure { none, file, structure, code, data, conflict, syw2x, memory };
struct WideSection {
    char name[8];
    uint32_t address, virtual_size, raw_size, characteristics;
    uint8_t sha256[32];
};
struct WideProfile {
    uint32_t entry, section_alignment, file_alignment, headers;
    uint16_t characteristics, dll_characteristics, subsystem;
    IMAGE_DATA_DIRECTORY directories[16];
    WideSection sections[4];
};
WideFailure check_widescreen_file(const uint8_t* bytes,size_t size,const WideProfile& profile);
const wchar_t* widescreen_failure_message(WideFailure failure);
}
