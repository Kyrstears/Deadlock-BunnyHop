#include "native_shared.hpp"
#include "third_party/minhook/MinHook.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

extern "C" void CommandTail();
extern "C" { void* original_command_tail = nullptr; }
namespace {
NativeState* state{};
uintptr_t client{}, pawn_reference{}, entity_reference{};
void* target{};
bool hook_created{};
volatile LONG running{};
uint64_t started{}, last_jump{}, dash_until{}, ground_since{};
uintptr_t previous_pawn{};
bool fired{}, saw_air{}, calibration_jumped{}, pending_takeoff{};
uint32_t jump_mask{};

template<class T> bool read(uintptr_t p, T& out) {
    SIZE_T n{};
    return p >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(p),
        &out, sizeof(out), &n) && n == sizeof(out);
}
template<class T> bool write(uintptr_t p, const T& v) {
    SIZE_T n{};
    return p >= 0x10000 && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(p),
        &v, sizeof(v), &n) && n == sizeof(v);
}
bool bit_or(uintptr_t p, uint32_t mask) {
    uint32_t v{}; return read(p, v) && write(p, v | mask);
}
void fail(const wchar_t* error) {
    if (!state) return;
    wcsncpy_s(state->error, error, _TRUNCATE);
    InterlockedIncrement(&state->errors);
    InterlockedExchange(&state->stop, 1);
}
bool foreground() {
    DWORD pid{}; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

uint32_t resolve_jump(uintptr_t pawn) {
    uintptr_t ges{}, handles{};
    int count{};
    if (!read(entity_reference, ges) || !ges
        || !read(pawn + 0x1448 + 0x68, count) || count <= 0 || count > 64
        || !read(pawn + 0x1448 + 0x70, handles) || !handles) return 0;
    for (int i = 0; i < count; ++i) {
        uint32_t h{}; uintptr_t chunk{}, entity{}, identity{}, name{};
        if (!read(handles + i*4, h) || h == 0xFFFFFFFF) continue;
        const auto index = h & 0x7FFF;
        if (!read(ges + 0x10 + 8*(index/512), chunk) || !chunk
            || !read(chunk + 0x70*(index%512), entity) || !entity
            || !read(entity + 0x10, identity) || !identity
            || !read(identity + 0x20, name) || !name) continue;
        std::array<char, 21> text{};
        if (!read(name, text) || std::memcmp(text.data(), "citadel_ability_jump", 21)) continue;
        int16_t slot{};
        if (read(entity + 0x784, slot) && slot >= 0 && slot < 32) return 1u << slot;
    }
    return 0;
}

uintptr_t unique(const std::vector<unsigned char>& image, std::initializer_list<int> pattern) {
    uintptr_t result{}; unsigned count{};
    for (size_t i = 0; i + pattern.size() <= image.size(); ++i) {
        bool matches = true; size_t j{};
        for (int b : pattern) { if (b >= 0 && image[i+j] != b) { matches=false; break; } ++j; }
        if (matches) { result=i; ++count; }
    }
    return count == 1 ? result : 0;
}
bool initialize() {
    client = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"client.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!client || !read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE
        || !read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.FileHeader.TimeDateStamp != 0x6ADF9754 || nt.OptionalHeader.SizeOfImage != 0x43BF000)
        return false;
    std::vector<unsigned char> image(nt.OptionalHeader.SizeOfImage);
    for (size_t i=0; i<image.size(); i+=4096) {
        SIZE_T n{};
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(client+i), image.data()+i,
            (std::min)(size_t{4096}, image.size()-i), &n);
    }
    auto pred = unique(image, {0x48,0x8D,0x05,-1,-1,-1,-1,0xC3,0xCC,0xCC,0xCC,0xCC,
                              0xCC,0xCC,0xCC,0xCC,0x40,0x53,0x56,0x41,0x54});
    auto chain = unique(image, {0x4C,0x39,0xB6,-1,-1,-1,-1,0x74,-1,0x44,0x88,0xBE});
    auto ges = unique(image, {0x48,0x8B,0x1D,-1,-1,-1,-1,0x48,0x89,0x1D,-1,-1,-1,-1,0x4C,0x63,0xB3});
    if (!pred || !chain || !ges) return false;
    int32_t relative{}, field{};
    std::memcpy(&relative, image.data()+pred+3, 4);
    std::memcpy(&field, image.data()+chain+3, 4);
    const int64_t pawn_rva = int64_t(pred)+7+relative+field;
    std::memcpy(&relative, image.data()+ges+3, 4);
    const int64_t ges_rva = int64_t(ges)+7+relative;
    if (pawn_rva <= 0 || ges_rva <= 0 || pawn_rva+8 >= int64_t(image.size())
        || ges_rva+8 >= int64_t(image.size())) return false;
    pawn_reference = client + pawn_rva; entity_reference = client + ges_rva;
    const unsigned char expected[] = {0x48,0x8B,0x4C,0x24,0x40,0x4C,0x8B,0xB4,0x24,0xC0,0x01,0x00,0x00};
    if (std::memcmp(image.data()+0x193A6D4, expected, sizeof(expected))) return false;
    target = reinterpret_cast<void*>(client+0x193A6D4);
    if (MH_Initialize() != MH_OK) return false;
    if (MH_CreateHook(target, CommandTail, &original_command_tail) != MH_OK) return false;
    hook_created = true;
    return true;
}
}

extern "C" void ProcessCommand(uintptr_t command) {
    auto* s = state;
    if (!s || s->stop || !s->enabled) return;
    const bool calibrate = s->calibrate != 0;
    if (!calibrate && !foreground()) return;
    const uint64_t now = GetTickCount64(), elapsed = now-started;
    if (calibrate && elapsed > 2300) { InterlockedExchange(&s->stop,1); return; }
    uintptr_t cvt{}, base{}, bvt{}, pawn{}, movement{}, angles{};
    uint32_t flags{}, ground{}; uint8_t life{}, mode{}, cursor{}, interrupt{};
    int health{}; float stamina{}, maximum{}, f{}, l{}, yaw{}, pitch{};
    struct Vec { float x,y,z; } velocity{};
    if (!read(command, cvt) || cvt != client+0x27DBED0
        || !read(command+0x48, base) || !base || !read(base,bvt) || bvt != client+0x289A800) {
        fail(L"Unexpected command layout. Stopping."); return;
    }
    if (!read(pawn_reference,pawn) || !pawn || !read(pawn+0x354,health) || health <= 0
        || !read(pawn+0x35C,life) || life || !read(pawn+0x3FC,flags)
        || !read(pawn+0x52C,ground) || !read(pawn+0x400,velocity)
        || !read(pawn+0x522,mode) || mode != 2 || !read(pawn+0xEC0,movement) || !movement
        || !read(movement+0x2DA,cursor) || cursor || !read(pawn+0x1448+0xE4,interrupt) || interrupt
        || !read(pawn+0x1448+0xE8+8,stamina) || !read(pawn+0x1448+0xE8+0x10,maximum)
        || !read(base+0x58,f) || !read(base+0x5C,l) || !read(base+0x40,angles) || !angles
        || !read(angles+0x1C,yaw) || !read(angles+0x18,pitch)) return;
    if (!std::isfinite(stamina) || !std::isfinite(maximum) || maximum<=0 || maximum>100
        || stamina<0 || stamina>maximum+.1f || !std::isfinite(yaw) || std::abs(yaw)>1000000
        || !std::isfinite(pitch) || std::abs(pitch)>90.1f
        || !std::isfinite(f) || !std::isfinite(l) || std::abs(f)>1.1f || std::abs(l)>1.1f
        || !std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z)
        || std::hypot(velocity.x,velocity.y)>10000) return;
    if (pawn != previous_pawn) {
        previous_pawn=pawn; fired=saw_air=pending_takeoff=false; calibration_jumped=false;
        ground_since=0;
        jump_mask=resolve_jump(pawn); dash_until=0;
    }
    if (!jump_mask) return;
    InterlockedIncrement(&s->commands);
    const bool grounded = (flags&1) && ground != 0xFFFFFFFF && std::abs(velocity.z)<1;
    const bool airborne = !(flags&1) && ground == 0xFFFFFFFF;
    const float speed = std::hypot(velocity.x,velocity.y);
    s->speed=speed; s->stamina=stamina;
    if (s->commands == 1) {
        s->yaw_start=yaw; s->stamina_start=stamina; s->stamina_min=stamina;
    }
    s->yaw_delta=(std::max)(float(s->yaw_delta),std::abs(analog::wrap(yaw-s->yaw_start)));
    s->stamina_min=(std::min)(float(s->stamina_min),stamina);
    if (airborne && pending_takeoff) {
        s->entry_speed=speed; s->peak_speed=speed;
        pending_takeoff=false;
        InterlockedIncrement(&s->takeoffs);
    }
    if (airborne) s->peak_speed=(std::max)(float(s->peak_speed),speed);
    if (!grounded) ground_since=0;
    else if (!ground_since) ground_since=now;
    if (airborne && fired && now-last_jump>=80) saw_air=true;
    const bool settled = grounded && now-ground_since>=8;
    if (settled && saw_air && now-last_jump>=150) { fired=false; saw_air=false; }
    if (!calibrate && down(VK_SHIFT)) dash_until=now+650;
    // Track landings even when Space is released.
    if (!analog::active(s->enabled != 0, down(VK_SPACE), calibrate)) return;
    if (!calibrate) {
        // Clear manual jump requests before adding a jump from the ground.
        analog::JumpInput input{};
        if (!read(command+0x60,input.execute)) { fail(L"Cannot read the jump request."); return; }
        for (int i=0;i<3;++i) if (!read(command+0x78+8*i,input.buttons[i])) {
            fail(L"Cannot read jump button state."); return;
        }
        const auto filtered=analog::filter_jump(input,jump_mask);
        if (!write(command+0x60,filtered.execute) || !bit_or(command+0x20,8)) {
            fail(L"Cannot update the jump request."); return;
        }
        for (int i=0;i<3;++i) if (!write(command+0x78+8*i,filtered.buttons[i])) {
            fail(L"Cannot update jump button state."); return;
        }
        uintptr_t buttons{};
        if (!read(base+0x38,buttons)) { fail(L"Cannot read the jump message."); return; }
        if (buttons) {
            for (int i=0;i<3;++i) {
                uint64_t bits{};
                if (!read(buttons+0x18+8*i,bits) || !write(buttons+0x18+8*i,bits&~uint64_t{2})) {
                    fail(L"Cannot filter the jump message."); return;
                }
            }
            if (!bit_or(buttons+0x10,7) || !bit_or(base+0x10,2)) {
                fail(L"Cannot update jump message flags."); return;
            }
        }
    }
    const bool manual = !calibrate && now<dash_until;
    bool hop = !manual && settled && !fired;
    if (calibrate) {
        f=elapsed<1800?-1.f:0.f; l=0;
        hop=settled && elapsed>=700 && !calibration_jumped;
    }
    if (hop) {
        uint32_t execute{};
        if (!read(command+0x60,execute) || !write(command+0x60,execute|jump_mask)
            || !bit_or(command+0x20,8)) { fail(L"Cannot write the jump command."); return; }
        uint64_t bits{}; uintptr_t buttons{};
        if (!read(command+0x78,bits) || !write(command+0x78,bits|uint64_t{2})
            || !read(base+0x38,buttons) || (buttons && (!write(buttons+0x18,bits|uint64_t{2})
            || !bit_or(buttons+0x10,1) || !bit_or(base+0x10,2)))) {
            fail(L"Cannot update jump button state."); return;
        }
        fired=true; last_jump=now; calibration_jumped=true; pending_takeoff=true;
        InterlockedIncrement(&s->jumps);
    }
    bool steering = s->strafe && airborne && !manual && std::hypot(f,l)>.01f;
    if (steering) {
        auto out=analog::strafe(f,l,yaw,velocity.x,velocity.y); f=out.forward; l=out.left;
    }
    if (steering || calibrate) {
        if (!write(base+0x58,f) || !write(base+0x5C,l) || !bit_or(base+0x10,0xC0)) {
            fail(L"Cannot write the movement command."); return;
        }
        uint64_t bits{}; uintptr_t buttons{};
        if (!read(command+0x78,bits) || !read(base+0x38,buttons)) { fail(L"Cannot read movement button state."); return; }
        bits &= ~uint64_t(8|16|512|1024);
        bits |= f>.01f?8:f<-.01f?16:0; bits |= l>.01f?512:l<-.01f?1024:0;
        if (!write(command+0x78,bits) || (buttons && (!write(buttons+0x18,bits)
            || !bit_or(buttons+0x10,1) || !bit_or(base+0x10,2)))) fail(L"Cannot write movement button state.");
    }
}

extern "C" __declspec(dllexport) DWORD WINAPI Start(void*) {
    if (InterlockedCompareExchange(&running,1,0)) return 1;
    wchar_t name[96]{}; mapping_name(name,GetCurrentProcessId());
    HANDLE map=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
    if (!map) { InterlockedExchange(&running,0); return 2; }
    auto* mapped=static_cast<NativeState*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeState)));
    if (!mapped || mapped->version != 1) {
        if(mapped)UnmapViewOfFile(mapped); CloseHandle(map); InterlockedExchange(&running,0); return 3;
    }
    state=mapped; previous_pawn=0; calibration_jumped=false; started=GetTickCount64();
    if ((!hook_created && !initialize()) || MH_EnableHook(target)!=MH_OK) {
        fail(L"Unsupported game build or hook setup failed.");
    } else {
        InterlockedExchange(&state->ready,1);
        while (!state->stop && GetTickCount64()-uint64_t(state->heartbeat)<1500) Sleep(10);
        InterlockedExchange(&state->stop,1);
        MH_DisableHook(target);
        Sleep(100);
    }
    state=nullptr;
    UnmapViewOfFile(mapped); CloseHandle(map); InterlockedExchange(&running,0);
    // Retain the disabled trampoline until game exit to avoid in-flight references.
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*) {
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);
    return TRUE;
}
