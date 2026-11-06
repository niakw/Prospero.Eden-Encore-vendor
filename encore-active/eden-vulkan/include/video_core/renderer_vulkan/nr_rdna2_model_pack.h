// SPDX-License-Identifier: MIT
// Safe index parser for the MIT DLSSNR-AMD NRMODEL1 model-pack layout.
// This file does not contain, extract, or distribute model weights.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Eden::Graphics::NeuralRDNA2 {
struct NeuralModelEntry {
    std::string name;
    std::uint64_t offset{};
    std::uint64_t size{};
};
enum class NeuralModelError : std::uint8_t {
    Ok, ShortHeader, BadMagic, BadHeader, BadCount, TruncatedIndex,
    UnsafePath, DuplicateName, MisalignedData, OutOfRange, Overlap,
};
[[nodiscard]] inline bool ValidNeuralWeightName(
    std::string_view name) noexcept {
    if(name.empty()||name.size()>4096u||name.front()=='/'||
       name.back()=='/')return false;
    std::size_t begin=0;
    for(std::size_t i=0;i<=name.size();++i){
        if(i<name.size()){
            const auto ch=static_cast<unsigned char>(name[i]);
            if(ch==0u||ch<32u||ch==127u||ch=='\\'||ch==':')
                return false;
            if(ch!='/')continue;
        }
        if(i==begin)return false;
        const auto part=name.substr(begin,i-begin);
        if(part=="."||part=="..")return false;
        begin=i+1;
    }
    return true;
}
[[nodiscard]] inline bool NeuralRead32(
    std::span<const std::uint8_t> bytes,std::size_t& cursor,
    std::uint32_t& value) noexcept {
    if(cursor>bytes.size()||bytes.size()-cursor<4u)return false;
    value=0u;
    for(unsigned i=0;i<4u;++i)
        value|=std::uint32_t(bytes[cursor+i])<<(8u*i);
    cursor+=4u;
    return true;
}
[[nodiscard]] inline bool NeuralRead64(
    std::span<const std::uint8_t> bytes,std::size_t& cursor,
    std::uint64_t& value) noexcept {
    if(cursor>bytes.size()||bytes.size()-cursor<8u)return false;
    value=0u;
    for(unsigned i=0;i<8u;++i)
        value|=std::uint64_t(bytes[cursor+i])<<(8u*i);
    cursor+=8u;
    return true;
}
// Parse only the beginning of a model file; file_bytes is its complete size.
// The caller can read any returned entry via a checked seek/read of the
// original file, without keeping potentially gigabytes of model data in RAM.
// On failure, output is empty and no partial model index can be used.
[[nodiscard]] inline NeuralModelError ParseNeuralModelIndex(
    std::span<const std::uint8_t> header,
    std::uint64_t file_bytes,
    std::vector<NeuralModelEntry>& output) {
    output.clear();
    if(header.size()<16u)return NeuralModelError::ShortHeader;
    constexpr char magic[8]={'N','R','M','O','D','E','L','1'};
    if(std::memcmp(header.data(),magic,8u)!=0)
        return NeuralModelError::BadMagic;
    std::size_t cursor=8u;
    std::uint32_t count=0u,reserved=0u;
    if(!NeuralRead32(header,cursor,count)||
       !NeuralRead32(header,cursor,reserved)||reserved!=0u)
        return NeuralModelError::BadHeader;
    if(count==0u||count>4096u)return NeuralModelError::BadCount;
    std::vector<NeuralModelEntry> parsed;
    parsed.reserve(count);
    std::unordered_set<std::string> seen;
    for(std::uint32_t i=0;i<count;++i){
        std::uint32_t len=0u;
        if(!NeuralRead32(header,cursor,len))
            return NeuralModelError::TruncatedIndex;
        if(len==0u||len>4096u)return NeuralModelError::UnsafePath;
        if(cursor>header.size()||header.size()-cursor<
           std::size_t(len)+16u)return NeuralModelError::TruncatedIndex;
        const auto* ptr=reinterpret_cast<const char*>(header.data()+cursor);
        std::string name(ptr,ptr+len);
        if(!ValidNeuralWeightName(name))return NeuralModelError::UnsafePath;
        if(!seen.insert(name).second)return NeuralModelError::DuplicateName;
        cursor+=len;
        std::uint64_t off=0u,size=0u;
        if(!NeuralRead64(header,cursor,off)||
           !NeuralRead64(header,cursor,size))
            return NeuralModelError::TruncatedIndex;
        parsed.push_back({std::move(name),off,size});
    }
    const auto header_end=std::uint64_t(cursor);
    if(header_end>UINT64_MAX-255u)return NeuralModelError::OutOfRange;
    const auto first_data=(header_end+255u)&~std::uint64_t(255u);
    if(first_data>file_bytes)return NeuralModelError::OutOfRange;
    for(const auto& entry:parsed){
        if((entry.offset&255u)!=0u)
            return NeuralModelError::MisalignedData;
        if(entry.offset<first_data||entry.offset>file_bytes||
           entry.size>file_bytes-entry.offset)
            return NeuralModelError::OutOfRange;
    }
    std::vector<NeuralModelEntry> ordered=parsed;
    std::sort(ordered.begin(),ordered.end(),
              [](const auto& a,const auto& b){return a.offset<b.offset;});
    for(std::size_t i=1u;i<ordered.size();++i){
        const auto& prev=ordered[i-1u];
        if(ordered[i].offset<prev.offset+prev.size)
            return NeuralModelError::Overlap;
    }
    output=std::move(parsed);
    return NeuralModelError::Ok;
}
} // namespace Eden::Graphics::NeuralRDNA2
