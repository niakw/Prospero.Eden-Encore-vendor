// SPDX-License-Identifier: MIT
// CPU-side immutable staging arena for user-provided DLSSNR-AMD NRMODEL1
// tensors. No weight data is bundled, and no GPU upload is implied.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
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
        if(!valid||source.empty())return false;
        constexpr std::size_t kMaxArena=256ull<<20;
        if(bytes_.size()>kMaxArena-255u)return false;
        const auto aligned=(bytes_.size()+255u)&~std::size_t(255u);
        if(aligned>kMaxArena||source.size()>kMaxArena-aligned)
            return false;
        const NeuralWeightRange range{
            static_cast<std::uint32_t>(aligned),
            static_cast<std::uint32_t>(source.size())};
        // Reserve memory before touching the logical index.
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
    std::vector<std::uint8_t> bytes_;
    std::unordered_map<std::string,NeuralWeightRange> ranges_;
};
} // namespace Eden::Graphics::NeuralRDNA2
