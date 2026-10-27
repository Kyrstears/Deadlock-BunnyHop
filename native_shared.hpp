#pragma once
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <cwchar>
#include <array>

struct NativeState {
    LONG version = 1;
    volatile LONG ready{}, stop{}, enabled = 1, strafe = 1, calibrate{};
    volatile LONG64 heartbeat{};
    volatile LONG commands{}, jumps{}, takeoffs{}, errors{};
    volatile float speed{}, stamina{}, entry_speed{}, peak_speed{}, yaw_start{}, yaw_delta{};
    volatile float stamina_start{}, stamina_min{};
    wchar_t error[192]{};
};
inline void mapping_name(wchar_t (&name)[96], DWORD pid) {
    swprintf_s(name, L"Local\\DeadlockDirectional-%lu", pid);
}
namespace analog {
inline bool active(bool enabled, bool space_held, bool calibration) {
    return enabled && (space_held || calibration);
}
struct JumpInput {
    uint32_t execute;
    std::array<uint64_t,3> buttons;
};
inline JumpInput filter_jump(JumpInput input, uint32_t jump_mask) {
    input.execute &= ~jump_mask;
    for (auto& bits : input.buttons) bits &= ~uint64_t{2};
    return input;
}
constexpr float pi = 3.14159265358979323846f;
struct Move { float forward, left; };
inline float wrap(float x) { return std::remainder(x, 360.0f); }
inline Move strafe(float forward, float left, float yaw, float vx, float vy) {
    if (!std::isfinite(forward) || !std::isfinite(left) || !std::isfinite(yaw)
        || !std::isfinite(vx) || !std::isfinite(vy)) return {0, 0};
    if (std::hypot(forward, left) < .01f || std::hypot(vx, vy) < 100)
        return {forward, left};
    const float wish = yaw + std::atan2(left, forward) * 180 / pi;
    const float velocity = std::atan2(vy, vx) * 180 / pi;
    const float side = wrap(wish - velocity) > 0 ? 1.f : -1.f;
    const float angle = (velocity + side * 80.f - yaw) * pi / 180;
    return {std::cos(angle), std::sin(angle)};
}
}
