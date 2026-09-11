#pragma once
#include "syw2x.h"
#include <cstdint>
namespace hq {
// Slots 0..6 are single indices; 7..10 are the packed value's hex bytes,
// left to right. This display order does not imply an in-game shade order.
inline int palette_field(int slot) { return slot<7?slot+4:11; }
inline bool palette_value(const std::wstring& text,int slot,uint32_t& value) {
    if(slot<0 || slot>10 || !Syw2xConfig::valid(text,slot<7?255:0xffffffffULL)) return false;
    const auto start=text.find_first_not_of(L" \t");
    value=uint32_t(std::wcstoull(text.c_str()+start,nullptr,
        text.compare(start,2,L"0x")==0 || text.compare(start,2,L"0X")==0?16:10));
    return true;
}
inline int palette_byte(uint32_t value,int slot) { return (value>>(slot<7?0:(10-slot)*8))&255; }
inline uint32_t palette_replace(uint32_t value,int slot,int index) {
    const int shift=slot<7?0:(10-slot)*8;
    return (value&~(uint32_t(255)<<shift))|(uint32_t(index&255)<<shift);
}
}
