#pragma once
#include "../generated/model_data.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace igwref {
using Input=std::array<std::uint8_t,16>;
using Hidden=std::array<std::uint8_t,8>;
using Logits=std::array<std::uint8_t,3>;
inline std::uint8_t saturate(long x) {return static_cast<std::uint8_t>(std::max(0L,std::min(255L,x)));}
inline Input preprocess(const double* values,unsigned* clip_count=nullptr) {
    Input x{};
    for(unsigned i=0;i<16;++i) {
        if(!std::isfinite(values[i]) || !(igwmodel::Std[i]>0))throw std::runtime_error("invalid_feature_or_std");
        const double normalized=(values[i]-igwmodel::Mean[i])/igwmodel::Std[i];
        const long raw=std::lround(normalized/igwmodel::InputScale)+128;
        if(clip_count && (raw<0 || raw>255))++*clip_count;
        x[i]=saturate(raw);
    }
    return x;
}
inline Hidden hidden(const Input& x) {
    Hidden h{};
    for(unsigned o=0;o<8;++o) {
        std::int64_t acc=igwmodel::Bias1[o];
        for(unsigned i=0;i<16;++i)acc+=(static_cast<int>(x[i])-128)*
            (static_cast<int>(igwmodel::Weight1[o][i])-128);
        // FC output zero point is 0: saturation represents the nonnegative ReLU range.
        h[o]=saturate(std::lround(acc*igwmodel::Multiplier1));
    }
    return h;
}
inline Logits logits(const Hidden& h) {
    Logits y{};
    for(unsigned o=0;o<3;++o) {
        std::int64_t acc=igwmodel::Bias2[o];
        for(unsigned i=0;i<8;++i)acc+=static_cast<int>(h[i])*
            (static_cast<int>(igwmodel::Weight2[o][i])-128);
        y[o]=saturate(std::lround(acc*igwmodel::Multiplier2)+128);
    }
    return y;
}
inline unsigned argmax(const Logits& y) {return static_cast<unsigned>(std::max_element(y.begin(),y.end())-y.begin());}
inline void verify_export() {
    for(unsigned n=0;n<igwmodel::Count;++n) {
        unsigned clipped=0;auto x=preprocess(igwmodel::Features[n],&clipped);
        if(clipped!=static_cast<unsigned>(igwmodel::InputClippedDims[n]))throw std::runtime_error("export_clip_count_mismatch");
        auto h=hidden(x);auto y=logits(h);
        for(unsigned i=0;i<16;++i)if(x[i]!=igwmodel::GoldenInput[n][i])throw std::runtime_error("export_preprocess_mismatch");
        for(unsigned i=0;i<8;++i)if(h[i]!=igwmodel::GoldenHidden[n][i])throw std::runtime_error("export_hidden_mismatch");
        for(unsigned i=0;i<3;++i)if(y[i]!=igwmodel::GoldenLogits[n][i])throw std::runtime_error("export_logits_mismatch");
    }
    for(unsigned o=0;o<3;++o) {
        unsigned sum=0;for(unsigned i=0;i<8;++i)sum+=std::abs(static_cast<int>(igwmodel::Weight2[o][i])-128);
        const int bound=static_cast<int>(std::ceil(sum*igwmodel::Multiplier2))+1;
        if(bound!=igwmodel::LogitTolerance[o])throw std::runtime_error("export_error_budget_mismatch");
    }
}
}
