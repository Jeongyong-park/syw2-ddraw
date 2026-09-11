#pragma once
#include <cwchar>
namespace hq {
enum Scaling { Nearest=0, Bilinear=1, SharpBilinear=2, Integer=3 };
inline const wchar_t* scaling_name(int value) {
    switch(value) {
    case Bilinear: return L"bilinear";
    case SharpBilinear: return L"sharp-bilinear";
    case Integer: return L"integer";
    default: return L"nearest";
    }
}
inline int parse_scaling(const wchar_t* value, bool legacy_linear, bool legacy_present=true) {
    if(!value || !*value) return legacy_present ? (legacy_linear ? Bilinear : Nearest) : SharpBilinear;
    for(int i=Nearest;i<=Integer;++i)
        if(!_wcsicmp(value,scaling_name(i))) return i;
    return Nearest;
}
}
