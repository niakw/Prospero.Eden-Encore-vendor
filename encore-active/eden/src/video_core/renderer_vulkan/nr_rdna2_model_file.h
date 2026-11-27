// SPDX-License-Identifier: MIT
// Read user-supplied NRMODEL1 weights; no bundled/proprietary model files.
// First load and validate only the index, then stream individual named tensors.
// Intended for host-side staging; Vulkan buffer upload is a separate concern.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_pack.h"
#include "video_core/renderer_vulkan/nr_rdna2_tile_layout.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Eden::Graphics::NeuralRDNA2 {
class NeuralModelFile {
public:
    [[nodiscard]] bool Open(const std::string& file_path) {
        Reset();
        std::ifstream stream(file_path,std::ios::binary|std::ios::ate);
        if(!stream)return false;
        const std::streamoff length=static_cast<std::streamoff>(stream.tellg());
        if(length<16)return false;
        const auto total=static_cast<std::uint64_t>(length);
        constexpr std::uint64_t kMaxIndexBytes=20ull<<20;
        const auto prefix_size=static_cast<std::size_t>(
            std::min(total,kMaxIndexBytes));
        std::vector<std::uint8_t> prefix(prefix_size);
        stream.seekg(0,std::ios::beg);
        stream.read(reinterpret_cast<char*>(prefix.data()),
                    static_cast<std::streamsize>(prefix.size()));
        if(!stream||stream.gcount()!=
            static_cast<std::streamsize>(prefix.size()))return false;
        std::vector<NeuralModelEntry> parsed;
        if(ParseNeuralModelIndex(prefix,total,parsed)!=NeuralModelError::Ok)
            return false;
        file_path_=file_path;
        file_size_=total;
        entries_=std::move(parsed);
        return true;
    }
    void Reset() noexcept {
        file_path_.clear();
        file_size_=0u;
        entries_.clear();
    }
    [[nodiscard]] bool IsOpen() const noexcept {
        return !file_path_.empty();
    }
    [[nodiscard]] const std::vector<NeuralModelEntry>& Entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] const NeuralModelEntry* Find(
        std::string_view name) const noexcept {
        if(!IsOpen()||!ValidNeuralWeightName(name))return nullptr;
        for(const auto& e:entries_)if(e.name==name)return &e;
        return nullptr;
    }
    // Complete output or empty; never return partially-read weights.
    [[nodiscard]] bool LoadRaw(
        std::string_view name,std::vector<std::uint8_t>& output) const {
        output.clear();
        const auto* entry=Find(name);
        constexpr std::uint64_t kMaxTensor=256ull<<20;
        if(!entry||entry->size>kMaxTensor||entry->offset>file_size_||
           entry->size>file_size_-entry->offset)return false;
        if(entry->offset>static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max()))return false;
        std::ifstream stream(file_path_,std::ios::binary|std::ios::ate);
        if(!stream||static_cast<std::streamoff>(stream.tellg())<
            static_cast<std::streamoff>(0))return false;
        if(static_cast<std::uint64_t>(
            static_cast<std::streamoff>(stream.tellg()))!=file_size_)
            return false; // reject file replacement/truncation
        stream.seekg(static_cast<std::streamoff>(entry->offset),
                     std::ios::beg);
        if(!stream)return false;
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(entry->size));
        if(!bytes.empty()){
            stream.read(reinterpret_cast<char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
            if(!stream||stream.gcount()!=
                static_cast<std::streamsize>(bytes.size()))return false;
        }
        output=std::move(bytes);
        return true;
    }
    // NRMODEL1 stores original ROW-MAJOR matrix bytes; do not apply
    // 16x16 tile transforms here (the upstream Vulkan host tiles later).
    [[nodiscard]] bool LoadModelMatrix(
        std::string_view name,std::uint32_t rows,std::uint32_t columns,
        std::vector<std::uint8_t>& output) const {
        output.clear();
        if(!ValidNeuralTileShape(rows,columns))return false;
        const auto needed=std::size_t(std::uint64_t(rows)*columns);
        std::vector<std::uint8_t> raw;
        if(!LoadRaw(name,raw)||raw.size()!=needed)return false;
        output=std::move(raw);
        return true;
    }
    // Exact E4M3FN tile bytes -> linear; no rescaling or re-quantization.
    // Does not cover the INT4/GPTQ model variants.
    [[nodiscard]] bool LoadTiledMatrix(
        std::string_view name,std::uint32_t rows,std::uint32_t columns,
        std::vector<std::uint8_t>& output) const {
        output.clear();
        if(!ValidNeuralTileShape(rows,columns))return false;
        const auto size=std::size_t(std::uint64_t(rows)*columns);
        std::vector<std::uint8_t> tiled;
        if(!LoadRaw(name,tiled)||tiled.size()!=size)return false;
        std::vector<std::uint8_t> linear(size);
        if(NeuralTilesToRowMajor(tiled,linear,rows,columns)!=
           NeuralTileResult::Ok)return false;
        output=std::move(linear);
        return true;
    }
private:
    std::string file_path_;
    std::uint64_t file_size_{};
    std::vector<NeuralModelEntry> entries_;
};
} // namespace Eden::Graphics::NeuralRDNA2
