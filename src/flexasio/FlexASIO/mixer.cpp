#include "mixer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace flexasio {
namespace {
    double RowSum(const std::array<double, 8>& row) {
        double sum = 0;
        for (double value : row) {
            if (!std::isfinite(value)) throw std::runtime_error("mixer weights must be finite");
            sum += std::abs(value);
        }
        if (!std::isfinite(sum)) throw std::runtime_error("mixer weight sum is too large");
        return sum;
    }

    double Read(const std::byte* data, StereoMixer::Format format) noexcept {
        switch (format) {
        case StereoMixer::Format::Int16: {
            int16_t value; std::memcpy(&value, data, sizeof(value));
            return value / 32768.0;
        }
        case StereoMixer::Format::Int24: {
            const auto* p = reinterpret_cast<const unsigned char*>(data);
            const int32_t value = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
            return (value >= 0x800000 ? value - 0x1000000 : value) / 8388608.0;
        }
        case StereoMixer::Format::Int32: {
            int32_t value; std::memcpy(&value, data, sizeof(value));
            return value / 2147483648.0;
        }
        case StereoMixer::Format::Float32: {
            float value; std::memcpy(&value, data, sizeof(value));
            return std::isfinite(value) ? value : 0;
        }
        }
        return 0;
    }

    void Write(std::byte* data, StereoMixer::Format format, double value) noexcept {
        if (std::isnan(value)) value = 0;
        value = std::clamp(value, -1.0, 1.0);
        switch (format) {
        case StereoMixer::Format::Int16: {
            const auto sample = static_cast<int16_t>(std::clamp(std::round(value * 32768), -32768.0, 32767.0));
            std::memcpy(data, &sample, sizeof(sample)); break;
        }
        case StereoMixer::Format::Int24: {
            const auto sample = static_cast<uint32_t>(static_cast<int32_t>(std::clamp(std::round(value * 8388608), -8388608.0, 8388607.0)));
            auto* p = reinterpret_cast<unsigned char*>(data);
            p[0] = static_cast<unsigned char>(sample);
            p[1] = static_cast<unsigned char>(sample >> 8);
            p[2] = static_cast<unsigned char>(sample >> 16);
            break;
        }
        case StereoMixer::Format::Int32: {
            const auto sample = static_cast<int32_t>(std::clamp(std::round(value * 2147483648), -2147483648.0, 2147483647.0));
            std::memcpy(data, &sample, sizeof(sample)); break;
        }
        case StereoMixer::Format::Float32: {
            const auto sample = static_cast<float>(value);
            std::memcpy(data, &sample, sizeof(sample)); break;
        }
        }
    }
}

void StereoMixer::Validate(const MixerConfig& config) {
    const auto maximum = (std::max)({1.0, RowSum(config.left), RowSum(config.right)});
    if (!std::isfinite(config.gain) || config.gain < 0) throw std::runtime_error("mixer gain must be finite and non-negative");
    const double divisor = config.normalize ? maximum : 1;
    for (const auto& row : {config.left, config.right})
        for (double weight : row)
            if (!std::isfinite(weight / divisor * config.gain)) throw std::runtime_error("effective mixer weight is too large");
}

StereoMixer::StereoMixer(const MixerConfig& config, Format format) : format(format), weights{config.left, config.right} {
    Validate(config);
    const double divisor = config.normalize ? (std::max)({1.0, RowSum(config.left), RowSum(config.right)}) : 1;
    for (auto& row : weights)
        for (auto& weight : row) weight = weight / divisor * config.gain;
}

void StereoMixer::Process(const Sources& sources, std::byte* left, std::byte* right, size_t frames) const noexcept {
    if (!left || !right) return;
    const size_t size = format == Format::Int16 ? 2 : (format == Format::Int24 ? 3 : 4);
    for (size_t frame = 0; frame < frames; ++frame) {
        double l = 0, r = 0;
        for (size_t channel = 0; channel < sources.size(); ++channel) {
            if (!sources[channel]) continue;
            const double value = Read(sources[channel] + frame * size, format);
            l += value * weights[0][channel];
            r += value * weights[1][channel];
        }
        Write(left + frame * size, format, l);
        Write(right + frame * size, format, r);
    }
}
}
