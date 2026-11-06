// SPDX-License-Identifier: MIT
// Reproduce the MIT DLSSNR-AMD e4m3_requantise_table() conversion:
// f_to_e4m3(e4m3_to_f(code)) with tie-to-even scan in tinlayout.hpp.
// Exhaustive 256-code analysis: 0x00 -> 0x80, 0xff -> 0x7f,
// every other code remains unchanged (including 0x80 and 0x7f).
#pragma once
#include <cstdint>
#include <span>
namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] constexpr std::uint8_t RequantizedE4M3(
    std::uint8_t code) noexcept {
    return code==0x00u?0x80u:code==0xffu?0x7fu:code;
}
inline void RequantizeE4M3(
    std::span<std::uint8_t> bytes) noexcept {
    for(auto& code:bytes)code=RequantizedE4M3(code);
}
} // namespace Eden::Graphics::NeuralRDNA2
