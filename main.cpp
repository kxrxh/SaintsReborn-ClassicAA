#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <atomic>
#include <string>
#include "wml.h"

namespace {
const WmlApi* api;
const WmlMod* self;
using Enable=void(*)(int);
using Status=int(*)();
Enable enable;
Status status;
std::wstring ini;
std::atomic<bool> wanted{true};
bool effective=false,bypass_menus=true,failed=false,reported=false;
ULONGLONG notice_until=0;
void Notice(const char* message){
    api->overlay_text(message);notice_until=GetTickCount64()+2500;
}
void Frame(void*){
    const bool gameplay=api->read_u32(0x8309ABEC)!=0 && api->read_u32(0x827B04D4)==0 &&
        api->read_u32(0x839E0DF8)==0 && api->read_u32(0x839E0FB0)==0;
    const bool desired=wanted.load(std::memory_order_relaxed) && !failed && (!bypass_menus || gameplay);
    if(desired!=effective){effective=desired;enable(desired?1:0);}
    const int state=status();
    if(state<0 && !failed){
        failed=true;effective=false;enable(0);
        api->log(self,"Native SMAA initialization failed; rendering remains unfiltered. See saintsrow_sdk.log.");
        Notice("Classic AA unavailable: check saintsrow_sdk.log");
    }else if(state==2 && !reported){
        reported=true;api->log(self,"Native SMAA 1x High is processing frames; original color grading preserved");
    }
    if(notice_until && GetTickCount64()>notice_until){notice_until=0;api->overlay_text("");}
}
}
extern "C" WML_EXPORT int ClassicAAGetEnabled(){
    return wanted.load(std::memory_order_relaxed)?1:0;
}
extern "C" WML_EXPORT void ClassicAASetEnabled(int value){
    const bool on=value!=0;
    wanted.store(on,std::memory_order_relaxed);
    WritePrivateProfileStringW(L"settings",L"enabled",on?L"1":L"0",ini.c_str());
    api->log(self,on?"Display setting: SMAA High":"Display setting: Off");
}
extern "C" WML_EXPORT int wml_mod_init(const WmlApi* loader,const WmlMod* mod){
    if(!loader || loader->version!=WML_API_VERSION ||
        loader->size<offsetof(WmlApi,overlay_text)+sizeof(loader->overlay_text) || !loader->overlay_text)return 1;
    api=loader;self=mod;
    const auto backend=GetModuleHandleW(L"rexgpu-xenos.dll");
    enable=backend?reinterpret_cast<Enable>(GetProcAddress(backend,"RexSmaaEnable")):nullptr;
    status=backend?reinterpret_cast<Status>(GetProcAddress(backend,"RexSmaaStatus")):nullptr;
    if(!enable || !status){api->log(self,"Native SMAA renderer extension is missing; Classic AA not loaded");return 1;}
    const int length=MultiByteToWideChar(CP_UTF8,0,mod->folder,-1,nullptr,0);
    if(length<=0)return 1;
    ini.resize(size_t(length));MultiByteToWideChar(CP_UTF8,0,mod->folder,-1,ini.data(),length);
    ini.pop_back();ini+=L"\\mod.ini";
    wanted.store(GetPrivateProfileIntW(L"settings",L"enabled",1,ini.c_str())!=0,std::memory_order_relaxed);
    bypass_menus=GetPrivateProfileIntW(L"settings",L"bypass_menus",1,ini.c_str())!=0;
    api->on_frame(Frame,nullptr);
    api->log(self,"Ready: native SMAA 1x High, controlled by Options > Display > Anti-aliasing");
    return 0;
}
