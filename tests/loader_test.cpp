// Exercise the original executable's DDRAW.dll import path, without game assets.
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <cstdio>
#include <cwchar>
#include <string>

static bool same_file(const wchar_t* left, const wchar_t* right) {
    const DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE a = CreateFileW(left, 0, sharing, nullptr, OPEN_EXISTING, 0, nullptr);
    if(a == INVALID_HANDLE_VALUE) return false;
    HANDLE b = CreateFileW(right, 0, sharing, nullptr, OPEN_EXISTING, 0, nullptr);
    BY_HANDLE_FILE_INFORMATION ai{}, bi{};
    const bool same = b != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(a, &ai) && GetFileInformationByHandle(b, &bi) &&
        ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber &&
        ai.nFileIndexHigh == bi.nFileIndexHigh && ai.nFileIndexLow == bi.nFileIndexLow;
    if(b != INVALID_HANDLE_VALUE) CloseHandle(b);
    CloseHandle(a);
    return same;
}

int wmain(int argc, wchar_t** argv) {
    // Exercise identity comparisons independently of the DLL import smoke test.
    if(argc == 4 && wcscmp(argv[1], L"--same-file") == 0)
        return same_file(argv[2], argv[3]) ? 0 : 3;
    if(argc != 1) return 7;
    wchar_t exe[32768]{}, loaded[32768]{};
    if(!GetModuleFileNameW(nullptr,exe,32768)) return 1;
    HMODULE dll=GetModuleHandleW(L"ddraw.dll");
    if(!dll || !GetModuleFileNameW(dll,loaded,32768)) return 2;
    std::wstring expected(exe);
    expected=expected.substr(0,expected.find_last_of(L"\\/")+1)+L"ddraw.dll";
    if(!same_file(expected.c_str(),loaded)) {
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
