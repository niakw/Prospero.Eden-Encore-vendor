// SPDX-License-Identifier: MIT
// DLSSNR-AMD model data adapters: FP16 -> FP32 scales/bias and
// NVIDIA 64x64 attention-bias fragment deswizzle. No model weights included.
#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline float NeuralHalfToFloat(std::uint16_t half) noexcept {
    const std::uint32_t sign=std::uint32_t(half&0x8000u)<<16u;
    std::uint32_t exp=(half>>10u)&31u;
    std::uint32_t mant=half&1023u;
    std::uint32_t bits=0u;
    if(exp==0u){
        if(mant==0u)bits=sign;
        else{
            int exponent=-14;
            while((mant&1024u)==0u){mant<<=1u;--exponent;}
            bits=sign|(std::uint32_t(exponent+127)<<23u)|
                ((mant&1023u)<<13u);
        }
    }else if(exp==31u){
        bits=sign|0x7f800000u|(mant<<13u);
    }else{
        bits=sign|((exp+112u)<<23u)|(mant<<13u);
    }
    return std::bit_cast<float>(bits);
}
[[nodiscard]] inline bool NeuralDecodeF16(
    std::span<const std::uint8_t> bytes,
    std::vector<float>& output) {
    output.clear();
    if(bytes.size()%2u!=0u||bytes.size()>(256ull<<20))return false;
    std::vector<float> result(bytes.size()/2u);
    for(std::size_t i=0;i<result.size();++i){
        const auto half=std::uint16_t(bytes[2u*i])|
                        (std::uint16_t(bytes[2u*i+1u])<<8u);
        const auto value=NeuralHalfToFloat(half);
        if(!std::isfinite(value))return false;
        result[i]=value;
    }
    output=std::move(result);
    return true;
}
// Bias source is [heads][4096] in NVIDIA MMA C-fragment order.
// Destination is [heads][query=64][key=64], exactly as the RDNA2
// window_attention_rdna2.comp shader indexes its bias storage buffer.
[[nodiscard]] inline bool NeuralDeswizzleBias64(
    std::span<const float> input,std::uint32_t heads,
    std::vector<float>& output) {
    output.clear();
    if(!heads||heads>16u||input.size()!=std::size_t(heads)*4096u)
        return false;
    std::vector<float> out(input.size());
    for(std::uint32_t h=0;h<heads;++h){
        for(std::uint32_t i=0;i<64u;++i){
            for(std::uint32_t j=0;j<64u;++j){
                const auto lane=4u*((i%16u)%8u)+((j%16u)%8u)/2u;
                const auto sl=(j%2u)|(((i%16u)/8u)<<1u)|
                              (((j%16u)/8u)<<2u);
                const auto src=((4u*(i/16u)+(j/16u))*256u)+lane*8u+sl;
                out[std::size_t(h)*4096u+i*64u+j]=
                    input[std::size_t(h)*4096u+src];
            }
        }
    }
    output=std::move(out);
    return true;
}
// The C32 original region can be [8 zero][C values][8 zero];
// some wider upsample layers are [C values][16 extras] without padding.
// The caller selects prefer_last only when the recovered plan requires it.
[[nodiscard]] inline bool NeuralResidualScale(
    std::span<const float> input,std::uint32_t channels,
    bool prefer_last,std::vector<float>& output) {
    output.clear();
    if(!channels||channels>256u||input.size()<channels)return false;
    bool padded=input.size()>=8u;
    for(std::size_t i=0;i<8u&&padded;++i)
        padded=(input[i]==0.0f);
    const auto start=padded?8u:
        (prefer_last?input.size()-channels:0u);
    if(start>input.size()||input.size()-start<channels)return false;
    std::vector<float> out(input.begin()+start,input.begin()+start+channels);
    for(float x:out)if(!std::isfinite(x))return false;
    output=std::move(out);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
