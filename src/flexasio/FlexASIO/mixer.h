#pragma once

#include <array>
#include <cstddef>

namespace flexasio {

struct MixerConfig {
    bool enabled = false;
    std::array<double, 8> left{1, 0, 0.7071, 0.5, 0.7071, 0, 0.7071, 0};
    std::array<double, 8> right{0, 1, 0.7071, 0.5, 0, 0.7071, 0, 0.7071};
    bool normalize = true;
    double gain = 1;

    bool operator==(const MixerConfig&) const = default;
};

// Independent of ASIO/PortAudio so the DSP can be tested without an audio device.
class StereoMixer {
public:
    enum class Format { Int16, Int24, Int32, Float32 };
    using Sources = std::array<const std::byte*, 8>;

    StereoMixer(const MixerConfig& config, Format format);
    void Process(const Sources& sources, std::byte* left, std::byte* right, size_t frames) const noexcept;
    static void Validate(const MixerConfig& config);

private:
    const Format format;
    std::array<std::array<double, 8>, 2> weights;
};

}
