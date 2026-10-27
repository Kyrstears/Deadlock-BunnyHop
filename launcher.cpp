#include "native_shared.hpp"
#include <tlhelp32.h>
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

void require(bool ok,const char* error) { if(!ok)throw std::runtime_error(error); }
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;
};
struct Module { uintptr_t base{}; std::wstring path; };
Module module(DWORD pid,const wchar_t* wanted) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    if(snapshot.value==INVALID_HANDLE_VALUE)return {};
    MODULEENTRY32W m{};m.dwSize=sizeof(m);
    if(Module32FirstW(snapshot.value,&m))do{
        if(!_wcsicmp(m.szModule,wanted))return {reinterpret_cast<uintptr_t>(m.modBaseAddr),m.szExePath};
    }while(Module32NextW(snapshot.value,&m));
    return {};
}
DWORD game_pid() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    PROCESSENTRY32W p{};p.dwSize=sizeof(p);
    if(snapshot.value!=INVALID_HANDLE_VALUE && Process32FirstW(snapshot.value,&p))do{
        if(!_wcsicmp(p.szExeFile,L"deadlock.exe"))return p.th32ProcessID;
    }while(Process32NextW(snapshot.value,&p));
    return 0;
}
std::string sha256(const std::wstring& path) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Cannot start the SHA256 check.");
    std::array<unsigned char,32> digest{};
    NTSTATUS status=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
    if(status<0){BCryptCloseAlgorithmProvider(algorithm,0);throw std::runtime_error("Cannot make the SHA256 hash.");}
    std::ifstream input{std::filesystem::path(path),std::ios::binary};
    std::array<char,65536> buffer{};
    while(input && status>=0) {
        input.read(buffer.data(),buffer.size());
        auto n=input.gcount();
        if(n)status=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(n),0);
    }
    bool complete=input.eof() && status>=0;
    if(complete)status=BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0);
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    require(complete && status>=0,"Cannot read client.dll for the SHA256 check.");
    std::string result;
    for(auto b:digest){char text[3];sprintf_s(text,"%02x",b);result+=text;}
    return result;
}
uintptr_t remote_system_function(DWORD pid,const char* name) {
    auto address=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);
    HMODULE owner{};
    require(address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        |GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&owner),
        "Cannot find the system function.");
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(owner,path,MAX_PATH);
    auto remote=module(pid,std::filesystem::path(path).filename().c_str());
    require(remote.base!=0,"Cannot find the system module in Deadlock.");
    return remote.base+reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(owner);
}
volatile LONG stop_requested{};
BOOL WINAPI console_control(DWORD event) {
    if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT||event==CTRL_CLOSE_EVENT){
        InterlockedExchange(&stop_requested,1);return TRUE;
    }
    return FALSE;
}
void self_test() {
    require(!analog::active(true,false,false),"In normal operation, the controller must operate only if you hold Space.");
    require(analog::active(true,true,false),"In normal operation, the controller must operate when you hold Space.");
    require(!analog::active(false,true,false),"The controller must not operate when it is paused, even if you hold Space.");
    require(analog::active(true,false,true),"In calibration mode, the controller must operate when you do not hold Space.");
    const analog::JumpInput input{(1u<<12)|(1u<<3),{2|8|1,2|16,2|512}};
    const auto filtered=analog::filter_jump(input,1u<<12);
    require(filtered.execute==(1u<<3),"The jump filter must keep the other abilities.");
    require(filtered.buttons==std::array<uint64_t,3>{8|1,16,512},
        "The jump filter must clear the jump bits. It must keep the other buttons.");
    for(int yaw=-180;yaw<=180;yaw+=15)for(int heading=-180;heading<=180;heading+=15)
        for(auto intent: {analog::Move{1,0},analog::Move{-1,0},analog::Move{0,1},
                          analog::Move{0,-1},analog::Move{1,1},analog::Move{-1,-1}}) {
            const float radians=heading*analog::pi/180;
            const float vx=400*std::cos(radians),vy=400*std::sin(radians);
            const auto out=analog::strafe(intent.forward,intent.left,float(yaw),vx,vy);
            require(std::abs(std::hypot(out.forward,out.left)-1)<.0001f,"The steering output must have a length of 1.");
            const float world=(yaw+std::atan2(out.left,out.forward)*180/analog::pi)*analog::pi/180;
            const float projection=(vx*std::cos(world)+vy*std::sin(world))/400;
            require(std::abs(projection-std::cos(80*analog::pi/180))<.0001f,"The acceleration angle must be 80 degrees.");
        }
    const auto idle=analog::strafe(0,0,90,400,100);
    require(idle.forward==0 && idle.left==0,"The controller must not steer when there is no movement input.");
    std::puts("The input and steering checks passed.");
}
int main(int argc,char** argv) {
    try {
        bool calibrate=false;
        if(argc==2 && !std::strcmp(argv[1],"--self-test")){self_test();return 0;}
        if(argc==2 && !std::strcmp(argv[1],"--calibrate"))calibrate=true;
        else if(argc>1){std::puts("Usage: deadlock-directional.exe [--calibrate|--self-test]");return 2;}
        const DWORD pid=game_pid();require(pid!=0,"Start Deadlock. Then run DLBHop.");
        const auto client=module(pid,L"client.dll");require(client.base!=0,"Deadlock did not load client.dll. Start DLBHop after Deadlock loads.");
        require(sha256(client.path)=="8da25ddcd11c1146eeb9770bca24fb25c594c7abdb28947035a44ec8f2945843",
            "DLBHop does not support this Deadlock build. It cannot start.");
        wchar_t own[MAX_PATH]{};GetModuleFileNameW(nullptr,own,MAX_PATH);
        const auto dll=(std::filesystem::path(own).parent_path()/L"deadlock-directional-hold.dll").wstring();
        require(std::filesystem::is_regular_file(dll),"Keep deadlock-directional-hold.dll in the same folder as this executable.");
        wchar_t mapname[96]{};mapping_name(mapname,pid);
        Handle map(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(NativeState),mapname));
        require(map.value!=nullptr,"Cannot create the shared control memory.");
        require(GetLastError()!=ERROR_ALREADY_EXISTS,"Another copy of DLBHop is active.");
        auto* control=static_cast<NativeState*>(MapViewOfFile(map.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeState)));
        require(control!=nullptr,"Cannot access the shared control memory.");
        *control=NativeState{};
        control->calibrate=calibrate;
        InterlockedExchange64(&control->heartbeat,LONG64(GetTickCount64()));
        Handle process(OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION
            |PROCESS_VM_WRITE|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid));
        require(process.value!=nullptr,"Cannot open the Deadlock process.");
        auto native=module(pid,L"deadlock-directional-hold.dll");
        if(!native.base) {
            const auto bytes=(dll.size()+1)*sizeof(wchar_t);
            void* memory=VirtualAllocEx(process.value,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
            require(memory!=nullptr,"Cannot allocate memory for the DLL path.");
            SIZE_T n{};
            const bool written=WriteProcessMemory(process.value,memory,dll.c_str(),bytes,&n)&&n==bytes;
            if(!written){VirtualFreeEx(process.value,memory,0,MEM_RELEASE);throw std::runtime_error("Cannot copy the DLL path.");}
            Handle load(CreateRemoteThread(process.value,nullptr,0,
                reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_system_function(pid,"LoadLibraryW")),memory,0,nullptr));
            require(load.value!=nullptr,"Cannot start the DLL loader.");
            const auto waited=WaitForSingleObject(load.value,10000);
            if(waited==WAIT_OBJECT_0)VirtualFreeEx(process.value,memory,0,MEM_RELEASE);
            require(waited==WAIT_OBJECT_0,"The DLL loader did not finish. DLBHop stopped.");
            native=module(pid,L"deadlock-directional-hold.dll");
            require(native.base!=0,"The DLL did not load.");
        }
        HMODULE local=LoadLibraryExW(dll.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
        require(local!=nullptr,"Cannot read the DLL exports.");
        auto start=GetProcAddress(local,"Start");
        const auto offset=reinterpret_cast<uintptr_t>(start)-reinterpret_cast<uintptr_t>(local);
        FreeLibrary(local);require(start!=nullptr,"The DLL has no Start export.");
        Handle worker(CreateRemoteThread(process.value,nullptr,0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(native.base+offset),nullptr,0,nullptr));
        require(worker.value!=nullptr,"Cannot start the controller.");
        SetConsoleCtrlHandler(console_control,TRUE);
        std::puts("DLBHop | Hold Space to hop and strafe. Release Space to return to normal movement.");
        std::puts("F7: Turn air strafe on or off | F8: Pause or resume | End: Stop the controller");
        if(calibrate)std::puts("Calibration: Move back. Jump once. Then strafe. The controller stops after 2.3 seconds.");
        bool f7was=false,f8was=false;
        while(WaitForSingleObject(worker.value,50)==WAIT_TIMEOUT) {
            InterlockedExchange64(&control->heartbeat,LONG64(GetTickCount64()));
            bool f7=(GetAsyncKeyState(VK_F7)&0x8000)!=0,f8=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
            if(f7&&!f7was)InterlockedExchange(&control->strafe,!control->strafe);
            if(f8&&!f8was)InterlockedExchange(&control->enabled,!control->enabled);
            f7was=f7;f8was=f8;
            if(stop_requested || (GetAsyncKeyState(VK_END)&0x8000))InterlockedExchange(&control->stop,1);
            std::printf("\r%s | strafe %s | speed %.1f | stamina %.2f | jumps %ld | takeoffs %ld     ",
                control->enabled?"READY":"PAUSED",control->strafe?"ON":"OFF",double(control->speed),
                double(control->stamina),control->jumps,control->takeoffs);
            std::fflush(stdout);
        }
        DWORD exit_code{};GetExitCodeThread(worker.value,&exit_code);
        std::printf("\nStopped | takeoff speed %.2f | peak speed %.2f | yaw change %.4f | stamina start %.2f / min %.2f | errors %ld\n",
            double(control->entry_speed),double(control->peak_speed),double(control->yaw_delta),
            double(control->stamina_start),double(control->stamina_min),control->errors);
        if(control->error[0])std::fwprintf(stderr,L"Error: %ls\n",control->error);
        const bool error=control->errors!=0 || exit_code!=0;
        SetConsoleCtrlHandler(console_control,FALSE);
        UnmapViewOfFile(control);
        return error?1:0;
    } catch(const std::exception& e){std::fprintf(stderr,"Error: %s\n",e.what());return 1;}
}
