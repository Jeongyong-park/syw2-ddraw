// Exercise the original executable's DDRAW.dll import path, without game assets.
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <cstdio>
#include <cwchar>
#include <string>
int wmain() {
    wchar_t exe[32768]{}, loaded[32768]{};
    if(!GetModuleFileNameW(nullptr,exe,32768)) return 1;
    HMODULE dll=GetModuleHandleW(L"ddraw.dll");
    if(!dll || !GetModuleFileNameW(dll,loaded,32768)) return 2;
    std::wstring expected(exe);
    expected=expected.substr(0,expected.find_last_of(L"\\/")+1)+L"ddraw.dll";
    if(_wcsicmp(expected.c_str(),loaded)) {
        std::fwprintf(stderr,L"Wrong DLL loaded: %ls\n",loaded); return 3;
    }
    IDirectDraw7* draw=nullptr;
    if(FAILED(DirectDrawCreateEx(nullptr,reinterpret_cast<void**>(&draw),IID_IDirectDraw7,nullptr)) || !draw) return 4;
    DDSURFACEDESC2 mode{}; mode.dwSize=sizeof(mode);
    if(FAILED(draw->GetDisplayMode(&mode))) { draw->Release(); return 5; }
    IDirectDrawClipper* clipper=nullptr;
    HRESULT result=draw->CreateClipper(0,&clipper,nullptr);
    if(clipper) clipper->Release();
    draw->Release();
    if(FAILED(result) || !clipper) return 6;
    std::puts("PASS: adjacent DDRAW.dll import, DirectDraw7 and system clipper");
    return 0;
}
