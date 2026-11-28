// SPDX-License-Identifier: MIT
// FSwin QKV matrix importer: NRMODEL1 [Q[C],K[C],V[C]] rows
// -> RDNA2 FSwin shader [head][Q[32],K[32],V[32]] rows.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline bool ReorderFSwinQkvRowMajor(
    std::span<const std::uint8_t> source,
    std::uint32_t channels,
    std::vector<std::uint8_t>& output) {
    if (channels != 32u && channels != 64u &&
        channels != 128u && channels != 256u) {
        output.clear();
        return false;
    }
    const std::size_t C = channels;
    if (source.size() != 3u * C * C) {
        output.clear();
        return false;
    }
    std::vector<std::uint8_t> result(source.size());
    for (std::size_t head = 0; head < C / 32u; ++head) {
        for (std::size_t part = 0; part < 3u; ++part) {
            for (std::size_t channel = 0; channel < 32u; ++channel) {
                const std::size_t src_row = part * C + head * 32u + channel;
                const std::size_t dst_row = head * 96u + part * 32u + channel;
                std::copy_n(source.data() + src_row * C, C,
                            result.data() + dst_row * C);
            }
        }
    }
    output = std::move(result);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
