// SPDX-License-Identifier: MIT
// RDNA2 neural E4M3FN model weights: explicit 16x16 tiled ↔ row-major.
// Layout from MIT DLSSNR-AMD coopmm.glsl / ffwd3_t.comp:
// tile group is [row_block][col_block][row_in_tile][col_in_tile].
// This operates on raw FP8 bytes; it does not interpret or quantize values.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Eden::Graphics::NeuralRDNA2 {
enum class NeuralTileResult : std::uint8_t {
    Ok,
    InvalidShape,
    InsufficientInput,
    InsufficientOutput,
    Aliasing,
};
[[nodiscard]] constexpr std::uint64_t Tile16Index(
    std::uint32_t row,std::uint32_t col,std::uint32_t columns) noexcept {
    return ((std::uint64_t(row/16u)*(columns/16u)+(col/16u))*256u)+
           std::uint64_t(row%16u)*16u+(col%16u);
}
[[nodiscard]] constexpr std::uint64_t RowMajorIndex(
    std::uint32_t row,std::uint32_t col,std::uint32_t columns) noexcept {
    return std::uint64_t(row)*columns+col;
}
[[nodiscard]] constexpr bool ValidNeuralTileShape(
    std::uint32_t rows,std::uint32_t columns) noexcept {
    constexpr std::uint64_t kMax=256ull<<20;
    return rows>0u && columns>0u && (rows%16u)==0u &&
        (columns%16u)==0u && std::uint64_t(rows)*columns<=kMax;
}
[[nodiscard]] inline bool OverlapNeuralTileRanges(
    const std::uint8_t* in,const std::uint8_t* out,
    std::size_t size) noexcept {
    const auto a=reinterpret_cast<std::uintptr_t>(in);
    const auto b=reinterpret_cast<std::uintptr_t>(out);
    if(a>std::numeric_limits<std::uintptr_t>::max()-size ||
       b>std::numeric_limits<std::uintptr_t>::max()-size)
        return true;
    return (a<b+size)&&(b<a+size);
}
[[nodiscard]] inline NeuralTileResult RepackNeuralTile16(
    std::span<const std::uint8_t> source,
    std::span<std::uint8_t> destination,
    std::uint32_t rows,std::uint32_t columns,
    bool tiled_to_linear) noexcept {
    if(!ValidNeuralTileShape(rows,columns))
        return NeuralTileResult::InvalidShape;
    const auto required=std::size_t(std::uint64_t(rows)*columns);
    if(source.size()<required)return NeuralTileResult::InsufficientInput;
    if(destination.size()<required)return NeuralTileResult::InsufficientOutput;
    if(OverlapNeuralTileRanges(source.data(),destination.data(),required))
        return NeuralTileResult::Aliasing;
    for(std::uint32_t row=0;row<rows;++row) {
        for(std::uint32_t col=0;col<columns;++col) {
            const auto tiled=Tile16Index(row,col,columns);
            const auto linear=RowMajorIndex(row,col,columns);
            if(tiled_to_linear)
                destination[linear]=source[tiled];
            else
                destination[tiled]=source[linear];
        }
    }
    return NeuralTileResult::Ok;
}
[[nodiscard]] inline NeuralTileResult NeuralTilesToRowMajor(
    std::span<const std::uint8_t> tiled,std::span<std::uint8_t> linear,
    std::uint32_t rows,std::uint32_t columns) noexcept {
    return RepackNeuralTile16(tiled,linear,rows,columns,true);
}
[[nodiscard]] inline NeuralTileResult RowMajorToNeuralTiles(
    std::span<const std::uint8_t> linear,std::span<std::uint8_t> tiled,
    std::uint32_t rows,std::uint32_t columns) noexcept {
    return RepackNeuralTile16(linear,tiled,rows,columns,false);
}
} // namespace Eden::Graphics::NeuralRDNA2
