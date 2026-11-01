#define _CRT_SECURE_NO_WARNINGS
#include "../FlexASIO/mixer.h"
#include "../FlexASIO/mixer_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace flexasio;
namespace {
    size_t checks = 0;
    void Check(bool condition, const char* message) {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    }
    template<class F> void Reject(F action) {
        bool rejected = false;
        try { action(); } catch (const std::exception&) { rejected = true; }
        Check(rejected, "expected invalid configuration to be rejected");
    }
    MixerConfig Parse(const std::string& source) {
        std::istringstream input(source);
        const auto result = toml::parse(input);
        if (!result.valid()) throw std::runtime_error(result.errorReason);
        return ParseMixerConfig(result.value.as<toml::Table>());
    }
    // Encode/decode test signals separately from the production conversion code.
    void Put(std::byte* p, StereoMixer::Format format, double value) {
        if (format == StereoMixer::Format::Float32) {
            float v = static_cast<float>(value); std::memcpy(p, &v, 4);
        } else {
            const int bits = format == StereoMixer::Format::Int16 ? 16 : (format == StereoMixer::Format::Int24 ? 24 : 32);
            const auto v = static_cast<uint32_t>(static_cast<int64_t>(value * std::ldexp(1.0, bits - 1)));
            for (int i = 0; i < bits / 8; ++i) p[i] = static_cast<std::byte>((v >> (8 * i)) & 255);
        }
    }
    double Get(const std::byte* p, StereoMixer::Format format) {
        if (format == StereoMixer::Format::Float32) {
            float v; std::memcpy(&v, p, 4); return v;
        }
        const int bits = format == StereoMixer::Format::Int16 ? 16 : (format == StereoMixer::Format::Int24 ? 24 : 32);
        int64_t raw = 0;
        for (int i = 0; i < bits / 8; ++i) raw |= int64_t(std::to_integer<unsigned char>(p[i])) << (8 * i);
        if (raw & (int64_t(1) << (bits - 1))) raw -= int64_t(1) << bits;
        return static_cast<double>(raw) / std::ldexp(1.0, bits - 1);
    }
    void TestFormat(StereoMixer::Format format) {
        constexpr size_t frames = 512;
        const size_t size = format == StereoMixer::Format::Int16 ? 2 : (format == StereoMixer::Format::Int24 ? 3 : 4);
        const double tolerance = format == StereoMixer::Format::Int16 ? 1.0 / 32768 : 0.000001;
        std::array<std::vector<std::byte>, 8> data;
        StereoMixer::Sources sources{};
        for (size_t ch = 0; ch < 8; ++ch) {
            data[ch].resize(frames * size);
            sources[ch] = data[ch].data();
        }
        // Canary bytes catch writes beyond the hardware's two output buffers.
        std::vector<std::byte> left(frames * size + 8, std::byte{0x5a}), right(left);
        MixerConfig config;
        const StereoMixer mixer(config, format);
        constexpr double divisor = 1 + 3 * 0.7071 + 0.5;
        for (size_t solo = 0; solo < 8; ++solo) {
            for (auto& channel : data) std::fill(channel.begin(), channel.end(), std::byte{0});
            for (size_t f = 0; f < frames; ++f) Put(data[solo].data() + f * size, format, f % 2 == 0 ? 0.5 : -0.5);
            mixer.Process(sources, left.data(), right.data(), frames);
            for (size_t f = 0; f < frames; ++f) {
                const double signal = f % 2 == 0 ? 0.5 : -0.5;
                Check(std::abs(Get(left.data() + f * size, format) - signal * config.left[solo] / divisor) <= tolerance, "solo left routing");
                Check(std::abs(Get(right.data() + f * size, format) - signal * config.right[solo] / divisor) <= tolerance, "solo right routing");
            }
        }
        for (double signal : {0.5, -1.0}) {
            for (auto& channel : data)
                for (size_t f = 0; f < frames; ++f) Put(channel.data() + f * size, format, signal);
            mixer.Process(sources, left.data(), right.data(), frames);
            Check(std::abs(Get(left.data(), format) - signal) <= tolerance, "all channels normalized");
            Check(std::abs(Get(right.data(), format) - signal) <= tolerance, "all channels normalized right");
        }
        // Only source 7 is active; its position must not collapse to channel zero.
        sources.fill(nullptr); sources[7] = data[7].data();
        mixer.Process(sources, left.data(), right.data(), frames);
        Check(Get(left.data(), format) == 0, "sparse channels left silence");
        Check(std::abs(Get(right.data(), format) + 0.7071 / divisor) <= tolerance, "sparse channel 7 right");
        sources.fill(nullptr);
        mixer.Process(sources, left.data(), right.data(), frames);
        Check(Get(left.data(), format) == 0 && Get(right.data(), format) == 0, "no active channels silence");
        config.normalize = false; config.gain = 4;
        const StereoMixer clipping(config, format);
        sources[0] = data[0].data(); sources[1] = data[1].data();
        for (double signal : {-1.0, 0.5}) {
            Put(data[0].data(), format, signal); Put(data[1].data(), format, signal);
            clipping.Process(sources, left.data(), right.data(), 1);
            const double expected = signal < 0 ? -1.0 : 1.0;
            Check(std::abs(Get(left.data(), format) - expected) <= tolerance, "saturation left");
            Check(std::abs(Get(right.data(), format) - expected) <= tolerance, "saturation right");
        }
        for (size_t i = frames * size; i < left.size(); ++i)
            Check(left[i] == std::byte{0x5a} && right[i] == std::byte{0x5a}, "output canary");
        clipping.Process(sources, nullptr, nullptr, frames);

        config.left.fill(0); config.right.fill(0);
        config.left[7] = -2; config.right[0] = 1;
        config.normalize = true; config.gain = 0.5;
        sources.fill(nullptr); sources[7] = data[7].data(); sources[0] = data[0].data();
        Put(data[7].data(), format, 0.5); Put(data[0].data(), format, 0.5);
        StereoMixer(config, format).Process(sources, left.data(), right.data(), 1);
        Check(std::abs(Get(left.data(), format) + 0.25) <= tolerance, "custom routing and signed weight");
        Check(std::abs(Get(right.data(), format) - 0.125) <= tolerance, "shared normalization preserves balance and applies gain");
        config.gain = 0;
        StereoMixer(config, format).Process(sources, left.data(), right.data(), 1);
        Check(Get(left.data(), format) == 0 && Get(right.data(), format) == 0, "gain zero mutes");
    }
    void TestConfig() {
        Check(!Parse("").enabled, "mixer disabled by default");
        Check(Parse("enabled=true\ngain=2\nnormalize=false").gain == 2, "integer gain accepted");
        const auto custom = Parse("enabled=true\nleft=[0,0,0,0,0,0,0,1]\nright=[1,0,0,0,0,0,0,0]");
        Check(custom.left[7] == 1 && custom.right[0] == 1, "custom integer rows");
        Check(Parse("enabled=true\nleft=[0.0,0.0,0.0,0.0,0.0,0.0,0.0,1.0]\nright=[1.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0]") == custom, "float rows equality");
        for (const auto* bad : {"enabled=1", "gain=-1", "gain=\"x\"", "normalize=1", "left=[1,2]", "left=[0,0,0,0,0,0,0,1]", "right=[0,0,0,0,0,0,0,1]", "enabeld=true"})
            Reject([&] { Parse(bad); });
        auto invalid = MixerConfig{};
        invalid.left[0] = std::numeric_limits<double>::infinity();
        Reject([&] { StereoMixer::Validate(invalid); });
        invalid.left[0] = std::numeric_limits<double>::quiet_NaN();
        Reject([&] { StereoMixer::Validate(invalid); });
        invalid = MixerConfig{}; invalid.gain = std::numeric_limits<double>::infinity();
        Reject([&] { StereoMixer::Validate(invalid); });
        invalid = MixerConfig{}; invalid.left.fill(std::numeric_limits<double>::max());
        Reject([&] { StereoMixer::Validate(invalid); });
        auto changed = MixerConfig{}; changed.enabled = true;
        Check(changed != MixerConfig{}, "enabled change detected");
        changed = MixerConfig{}; changed.left[3] = 0;
        Check(changed != MixerConfig{}, "matrix change detected");
    }
}

int main() {
    try {
        TestConfig();
        for (auto format : {StereoMixer::Format::Int32, StereoMixer::Format::Int16, StereoMixer::Format::Int24, StereoMixer::Format::Float32}) TestFormat(format);
        std::cout << "PASS: " << checks << " mixer checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
