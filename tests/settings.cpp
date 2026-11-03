#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include "wml.h"
#include "display_helpers.inc"
WmlFrameCallback frame=nullptr;
bool menu=false;
int key_reads=0;
void OnFrame(WmlFrameCallback fn,void*){frame=fn;}
void Log(const WmlMod*,const char*){}
void Overlay(const char*){}
int Key(int){++key_reads;return 0;}
uint32_t Read(uint32_t address){
    if(address==0x8309ABEC)return 0x90000000;
    if(address==0x839E0DF8)return menu?0x82FFB84C:0;
    return 0;
}
int main(){
    WmlApi api={};api.version=WML_API_VERSION;api.size=sizeof(api);api.log=Log;api.overlay_text=Overlay;api.read_u32=Read;api.on_frame=OnFrame;api.key_pressed=Key;
    const auto folder=std::filesystem::absolute("settings-test-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);const auto utf8=folder.u8string();
    std::string path(reinterpret_cast<const char*>(utf8.data()),utf8.size());WmlMod mod{"Classic AA",path.c_str()};
    assert(!ClassicAaAvailable());
    auto backend=LoadLibraryW(L"mock\\rexgpu-xenos.dll");assert(backend);
    using Get=int(*)();using Enable=void(*)(int);
    auto gpu=reinterpret_cast<Get>(GetProcAddress(backend,"TestSmaaEnabled"));assert(gpu);
    auto reset=reinterpret_cast<Enable>(GetProcAddress(backend,"RexSmaaEnable"));
    HMODULE plugin=nullptr;
    auto load=[&]{
        plugin=LoadLibraryW(L"ClassicAA.dll");assert(plugin);
        auto init=reinterpret_cast<WmlModInitFunction>(GetProcAddress(plugin,"wml_mod_init"));assert(init && init(&api,&mod)==0 && frame);
        assert(ClassicAaAvailable());
    };
    auto unload=[&]{frame=nullptr;FreeLibrary(plugin);assert(!ClassicAaAvailable());reset(0);};
    load();assert(ClassicAaChoice()==1);frame(nullptr);assert(gpu()==1);
    ApplyClassicAaChoice(0);assert(ClassicAaChoice()==0);frame(nullptr);assert(gpu()==0);
    const auto ini=(folder/L"mod.ini").wstring();assert(GetPrivateProfileIntW(L"settings",L"enabled",9,ini.c_str())==0);
    unload();load();assert(ClassicAaChoice()==0);frame(nullptr);assert(gpu()==0);
    menu=true;ApplyClassicAaChoice(1);frame(nullptr);assert(ClassicAaChoice()==1 && gpu()==0);
    assert(GetPrivateProfileIntW(L"settings",L"enabled",9,ini.c_str())==1);
    menu=false;frame(nullptr);assert(gpu()==1);
    unload();load();assert(ClassicAaChoice()==1);frame(nullptr);assert(gpu()==1);
    assert(key_reads==0);unload();FreeLibrary(backend);std::filesystem::remove_all(folder);
    std::cout<<"PASS: real Display-menu bridge controls the controller DLL, Off/On applies, preference persists across reloads, menus bypass rendering without changing preference, no hotkey reads\n";
}
