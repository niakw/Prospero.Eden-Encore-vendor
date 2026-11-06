// SPDX-License-Identifier: MIT
// CPU-side immutable staging arena for user-provided DLSSNR-AMD NRMODEL1
// tensors. No weight data is bundled, and no GPU upload is implied.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Eden::Graphics::NeuralRDNA2 {
struct NeuralWeightRange {
    std::uint32_t offset_bytes{};
    std::uint32_t length_bytes{};
};
class NeuralWeightArena {
public:
    void Clear() noexcept {
        bytes_.clear();
        ranges_.clear();
    }
    [[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept {
        return bytes_;
    }
    [[nodiscard]] const NeuralWeightRange* Find(
        std::string_view name) const {
        const auto it=ranges_.find(std::string(name));
        return it==ranges_.end()?nullptr:&it->second;
    }
    // Upload records are exact, 256-aligned byte ranges suitable for
    // a single storage buffer. Set layout/descriptor binding is separate.
    [[nodiscard]] bool AppendRaw(
        const NeuralModelFile& model,std::string_view name,
        NeuralWeightRange& recorded) {
        return Append(model,name,false,0u,0u,recorded);
    }
    [[nodiscard]] bool AppendTiled(
        const NeuralModelFile& model,std::string_view name,
        std::uint32_t rows,std::uint32_t columns,
        NeuralWeightRange& recorded) {
        return Append(model,name,true,rows,columns,recorded);
    }
    // The normal NRMODEL1 source is already row-major: preserve byte order.
    [[nodiscard]] bool AppendModelMatrix(
        const NeuralModelFile& model,std::string_view name,
        std::uint32_t rows,std::uint32_t columns,
        NeuralWeightRange& recorded) {
        recorded={};
        if(!ValidNeuralTileShape(rows,columns))return false;
        const auto* entry=model.Find(name);
        if(!entry||entry->size!=std::uint64_t(rows)*columns)return false;
        return AppendRaw(model,name,recorded);
    }
    // For decoded f16 bias/scales, append IEEE754 f32 in little-endian.
    [[nodiscard]] bool AppendFP32(
        std::string_view name,std::span<const float> values,
        NeuralWeightRange& recorded) {
        recorded={};
        if(values.empty()||values.size()>(256ull<<20)/4u)return false;
        std::vector<std::uint8_t> packed(values.size()*4u);
        for(std::size_t i=0;i<values.size();++i){
            if(!std::isfinite(values[i]))return false;
            const auto word=std::bit_cast<std::uint32_t>(values[i]);
            for(unsigned j=0;j<4u;++j)
                packed[i*4u+j]=std::uint8_t(word>>(8u*j));
        }
        return AppendBytes(name,packed,recorded);
    }
    [[nodiscard]] bool AppendBytes(
        std::string_view name,std::span<const std::uint8_t> source,
        NeuralWeightRange& recorded) {
        recorded={};
        if(name.empty()||Find(name)||source.empty())return false;
        constexpr std::size_t kMaxArena=256ull<<20;
        if(bytes_.size()>kMaxArena-255u)return false;
        const auto aligned=(bytes_.size()+255u)&~std::size_t(255u);
        if(aligned>kMaxArena||source.size()>kMaxArena-aligned)
            return false;
        const NeuralWeightRange range{
            static_cast<std::uint32_t>(aligned),
            static_cast<std::uint32_t>(source.size())};
        bytes_.reserve(aligned+source.size());
        const auto old_size=bytes_.size();
        bytes_.resize(aligned,0u);
        bytes_.insert(bytes_.end(),source.begin(),source.end());
        const auto inserted=ranges_.emplace(std::string(name),range);
        if(!inserted.second){
            bytes_.resize(old_size);
            return false;
        }
        recorded=range;
        return true;
    }
private:
    [[nodiscard]] bool Append(
        const NeuralModelFile& model,std::string_view name,
        bool tiled,std::uint32_t rows,std::uint32_t columns,
        NeuralWeightRange& recorded) {
        recorded={};
        if(name.empty()||Find(name))return false;
        std::vector<std::uint8_t> source;
        const bool valid=tiled?
            model.LoadTiledMatrix(name,rows,columns,source):
            model.LoadRaw(name,source);
        return valid&&AppendBytes(name,source,recorded);
    }
    std::vector<std::uint8_t> bytes_;
    std::unordered_map<std::string,NeuralWeightRange> ranges_;
};
} // namespace Eden::Graphics::NeuralRDNA2
