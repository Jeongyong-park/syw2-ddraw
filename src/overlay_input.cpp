#include "overlay_input.h"
#include <windows.h>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <vector>

namespace hq::overlay_input {
namespace {
bool (*dialog_open)()=nullptr;
bool settings_visible() { return dialog_open && dialog_open(); }
struct InputSlot { void** slot; void* original; void* replacement; };
std::vector<InputSlot> input_slots;
using KeyFn=SHORT (WINAPI*)(int);
using KeyboardFn=BOOL (WINAPI*)(PBYTE);
KeyFn original_async=::GetAsyncKeyState, original_key=::GetKeyState;
KeyboardFn original_keyboard=::GetKeyboardState;
using ShowCursorFn=int (WINAPI*)(BOOL);
using SetCursorFn=HCURSOR (WINAPI*)(HCURSOR);
ShowCursorFn original_show_cursor=::ShowCursor;
SetCursorFn original_set_cursor=::SetCursor;
bool overlay_cursor_active=false;
int cursor_lift=0;
HCURSOR game_cursor_shape=nullptr;
bool swallowed_keys[256]{};
}
void begin_cursor() {
    if(overlay_cursor_active) return;
    overlay_cursor_active=true;
    game_cursor_shape=GetCursor();
    // ShowCursor has a counter, not a boolean state. Add only what is necessary.
    int count=original_show_cursor(TRUE); ++cursor_lift;
    if(count>0) { original_show_cursor(FALSE); --cursor_lift; }
    else while(count<0) { count=original_show_cursor(TRUE); ++cursor_lift; }
    original_set_cursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
}
void end_cursor() {
    if(!overlay_cursor_active) return;
    overlay_cursor_active=false;
    while(cursor_lift>0) { original_show_cursor(FALSE); --cursor_lift; }
    original_set_cursor(game_cursor_shape);
}
static int WINAPI overlay_show_cursor(BOOL show) {
    int count=original_show_cursor(show);
    if(!overlay_cursor_active) return count;
    const int game_count=count-cursor_lift;
    while(count<0) { count=original_show_cursor(TRUE); ++cursor_lift; }
    return game_count;
}
static HCURSOR WINAPI overlay_set_cursor(HCURSOR cursor) {
    if(!overlay_cursor_active) return original_set_cursor(cursor);
    HCURSOR previous=game_cursor_shape; game_cursor_shape=cursor;
    original_set_cursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512))); return previous;
}
void begin_input() { std::fill(std::begin(swallowed_keys),std::end(swallowed_keys),true); }
static SHORT filter_key(int key,SHORT result) {
    if(key<0 || key>255) return result;
    if(settings_visible()) { swallowed_keys[key]=true; return 0; }
    if(swallowed_keys[key]) { if(!(result&0x8000)) swallowed_keys[key]=false; return 0; }
    return result;
}
static SHORT WINAPI overlay_async(int key) { return filter_key(key,original_async(key)); }
static SHORT WINAPI overlay_key(int key) { return filter_key(key,original_key(key)); }
static BOOL WINAPI overlay_keyboard(PBYTE keys) {
    BOOL ok=original_keyboard(keys);
    if(ok) for(int i=0;i<256;++i) {
        if(settings_visible() || swallowed_keys[i]) { filter_key(i,SHORT((keys[i]&0x80)<<8)); keys[i]=0; }
    }
    return ok;
}
void restore() {
    for(auto& i:input_slots) if(*i.slot==i.replacement) {
        DWORD old=0;
        if(VirtualProtect(i.slot,sizeof(void*),PAGE_READWRITE,&old)) {
            InterlockedExchangePointer(i.slot,i.original); DWORD ignored=0; VirtualProtect(i.slot,sizeof(void*),old,&ignored);
        }
    }
    input_slots.clear();
}
void install(bool (*is_open)()) {
    dialog_open=is_open;
    if(!input_slots.empty()) return;
    auto base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;imports->Name;++imports) {
        if(_stricmp(reinterpret_cast<char*>(base+imports->Name),"user32.dll") || !imports->OriginalFirstThunk) continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->OriginalFirstThunk);
        auto addresses=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->FirstThunk);
        for(;names->u1.AddressOfData;++names,++addresses) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData)->Name;
            auto slot=reinterpret_cast<void**>(&addresses->u1.Function); void* replacement=nullptr;
            if(!strcmp(reinterpret_cast<char*>(name),"GetAsyncKeyState")) { original_async=reinterpret_cast<KeyFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_async); }
            if(!strcmp(reinterpret_cast<char*>(name),"GetKeyState")) { original_key=reinterpret_cast<KeyFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_key); }
            if(!strcmp(reinterpret_cast<char*>(name),"GetKeyboardState")) { original_keyboard=reinterpret_cast<KeyboardFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_keyboard); }
            if(!strcmp(reinterpret_cast<char*>(name),"ShowCursor")) { original_show_cursor=reinterpret_cast<ShowCursorFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_show_cursor); }
            if(!strcmp(reinterpret_cast<char*>(name),"SetCursor")) { original_set_cursor=reinterpret_cast<SetCursorFn>(*slot); replacement=reinterpret_cast<void*>(&overlay_set_cursor); }
            if(!replacement) continue;
            DWORD old=0;
            if(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) {
                input_slots.push_back({slot,*slot,replacement});
                InterlockedExchangePointer(slot,replacement); DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
            }
        }
    }
}
}
