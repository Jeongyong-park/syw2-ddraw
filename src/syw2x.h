#pragma once
#include <windows.h>
#include <array>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cwctype>
#include <cwchar>
#include <cerrno>

namespace hq {
struct Syw2xOption { const wchar_t* section; const wchar_t* key; const wchar_t* label; const wchar_t* fallback; unsigned long long maximum; };
inline constexpr Syw2xOption syw2x_options[]={
    {L"ViewPortControl",L"ViewPortPlusOn",L"전장 확장 831×624",L"0",1},
    {L"ForDarkPannel",L"ProgressTextColorChange",L"어두운 패널 글자색",L"0",1},
    {L"StatusBarStyle",L"EmptyBarFillOn",L"상태바 빈 영역 채우기",L"1",1},
    {L"IdentificationFriendOrFoe",L"MyIFFOn",L"내 유닛 식별 색상",L"0",1},
    {L"StatusBarStyle",L"EmptyBarFillColor",L"빈 영역",L"0xFB",255},
    {L"StatusBarStyle",L"HealthBarFillColor",L"체력",L"0x44",255},
    {L"StatusBarStyle",L"ManaBarFillColor",L"마나",L"0xEF",255},
    {L"StatusBarStyle",L"ExpBarFillColor",L"경험치",L"0x2D",255},
    {L"StatusBarStyle",L"ShieldBarFillColor",L"보호막",L"0xDB",255},
    {L"StatusBarStyle",L"BarBorderColor",L"테두리",L"0x00",255},
    {L"IdentificationFriendOrFoe",L"MyMiniMapIFF",L"미니맵 식별",L"0x44",255},
    {L"IdentificationFriendOrFoe",L"MyBrightIFF",L"유닛 식별 (4색)",L"0xF4F3F2F1",0xffffffffULL},
};
struct Syw2xConfig {
    std::wstring path;
    std::array<std::wstring,12> values;
    std::vector<char> original;
    bool existed=false, readable=false;
    static bool read(const std::wstring& name,std::vector<char>& bytes,bool& exists) {
        const DWORD attr=GetFileAttributesW(name.c_str());
        exists=attr!=INVALID_FILE_ATTRIBUTES;
        if(!exists) return GetLastError()==ERROR_FILE_NOT_FOUND;
        std::ifstream input(std::filesystem::path(name),std::ios::binary|std::ios::ate);
        if(!input || input.tellg()<0 || input.tellg()>1024*1024) return false;
        bytes.resize(size_t(input.tellg())); input.seekg(0);
        return bytes.empty() || bool(input.read(bytes.data(),bytes.size()));
    }
    static bool valid(const std::wstring& value,unsigned long long maximum) {
        const auto start=value.find_first_not_of(L" \t");
        if(start==std::wstring::npos || value[start]==L'-' || value[start]==L'+') return false;
        wchar_t* end=nullptr; errno=0;
        const auto number=std::wcstoull(value.c_str()+start,&end,
            value.compare(start,2,L"0x")==0 || value.compare(start,2,L"0X")==0?16:10);
        if(errno==ERANGE || end==value.c_str()+start || number>maximum) return false;
        while(*end && std::iswspace(*end)) ++end;
        return !*end;
    }
    bool load(const std::wstring& name) {
        path=name; original.clear(); readable=read(path,original,existed);
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
        for(size_t i=0;i<values.size();++i) {
            wchar_t value[256]{}; const auto& o=syw2x_options[i];
            GetPrivateProfileStringW(o.section,o.key,o.fallback,value,256,path.c_str()); values[i]=value;
        }
        return readable;
    }
    bool save(const std::array<std::wstring,12>& next,std::wstring& status) {
        if(!readable) { status=L"설정 파일을 읽을 수 없어 저장하지 않았습니다."; return false; }
        bool changed=!existed;
        for(size_t i=0;i<next.size();++i) if(!existed || next[i]!=values[i]) {
            changed=true;
            if(!valid(next[i],syw2x_options[i].maximum)) {
                status=std::wstring(syw2x_options[i].label)+L": 숫자 범위를 확인하세요 (0~255, 4색은 0xFFFFFFFF까지)."; return false;
            }
        }
        if(!changed) { status=L"변경된 설정이 없습니다."; return true; }
        std::vector<char> current; bool exists=false;
        if(!read(path,current,exists) || exists!=existed || current!=original) {
            status=L"다른 프로그램에서 설정이 변경됐습니다. 창을 닫고 다시 열어 주세요."; return false;
        }
        const auto folder=std::filesystem::path(path).parent_path().wstring();
        wchar_t temp[MAX_PATH]{};
        if(!GetTempFileNameW(folder.c_str(),L"syw",0,temp)) { status=L"설정 폴더에 쓸 수 없습니다."; return false; }
        bool ok=!existed || CopyFileW(path.c_str(),temp,FALSE)!=FALSE;
        for(size_t i=0;ok && i<next.size();++i) if(!existed || next[i]!=values[i])
            ok=WritePrivateProfileStringW(syw2x_options[i].section,syw2x_options[i].key,next[i].c_str(),temp)!=FALSE;
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp);
        current.clear();
        ok=ok && read(path,current,exists) && exists==existed && current==original;
        if(ok) ok=existed?ReplaceFileW(path.c_str(),temp,nullptr,0,nullptr,nullptr)!=FALSE:
            MoveFileExW(temp,path.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
        SetFileAttributesW(temp,FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(temp);
        if(!ok) { status=L"저장하지 못했습니다. 파일 권한 또는 외부 설정 변경을 확인하세요."; return false; }
        load(path);
        status=L"저장했습니다. 게임을 완전히 종료한 뒤 다시 실행하면 적용됩니다."; return true;
    }
};
}
