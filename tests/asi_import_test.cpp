#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include "../src/asi_hook.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
HRESULT WINAPI intercepted(GUID*,void**,REFIID,IUnknown*) { return E_FAIL; }
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s (%lu)\n",__LINE__,#x,GetLastError()); return 1; } } while(0)
int wmain(int argc,wchar_t** argv) {
    CHECK(argc==2 || argc==3);
    const auto slot=hq::draw_import(GetModuleHandleW(nullptr)); CHECK(slot);
    auto before=*slot;
    if(argc==3 && std::wcscmp(argv[2],L"intercepted")==0) {
        DWORD old=0,ignored=0; CHECK(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old));
        *slot=reinterpret_cast<void*>(&intercepted); before=*slot;
        CHECK(VirtualProtect(slot,sizeof(void*),old,&ignored));
    } else if(argc==3) CHECK(LoadLibraryW(argv[2])); // Existing standalone wrapper.
    auto asi=LoadLibraryW(argv[1]); CHECK(asi);
    auto init=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(asi,"InitializeASI")); CHECK(init);
    CHECK(*slot==before); // DllMain must not install the hook.
    init(); const auto installed=*slot; init(); CHECK(*slot==installed);
    if(argc==3) { CHECK(installed==before); puts("PASS: duplicate wrapper refused"); return 0; }
    CHECK(installed!=before);
    CHECK(installed==reinterpret_cast<void*>(GetProcAddress(asi,"DirectDrawCreateEx")));
    IDirectDraw7* draw=nullptr;
    CHECK(SUCCEEDED(DirectDrawCreateEx(nullptr,reinterpret_cast<void**>(&draw),IID_IDirectDraw7,nullptr)));
    DDDEVICEIDENTIFIER2 device{}; CHECK(SUCCEEDED(draw->GetDeviceIdentifier(&device,0)));
    CHECK(std::strcmp(device.szDriver,"hqcdd.dll")==0);
    CHECK(draw->Release()==0);
    FreeLibrary(asi); CHECK(GetModuleHandleW(L"hqcdd.asi")); // Callback must remain pinned.
    puts("PASS: delayed, idempotent ASI IAT connection routes real DD7 imports and pins callbacks");
}
