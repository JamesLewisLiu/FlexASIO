#include "mixer_config.h"

#include <stdexcept>
#include <string>

namespace flexasio {
namespace {
    double Number(const toml::Value& value) {
        // TOML distinguishes integers and floats; both are valid mixer numbers.
        if (value.is<int64_t>()) return static_cast<double>(value.as<int64_t>());
        return value.as<double>();
    }
}

MixerConfig ParseMixerConfig(const toml::Table& table) {
    MixerConfig config;
    for (const auto& [key, value] : table) {
        try {
            if (key == "enabled") config.enabled = value.as<bool>();
            else if (key == "normalize") config.normalize = value.as<bool>();
            else if (key == "gain") config.gain = Number(value);
            else if (key == "left" || key == "right") {
                const auto& array = value.as<toml::Array>();
                if (array.size() != 8) throw std::runtime_error("expected exactly 8 weights");
                auto& row = key == "left" ? config.left : config.right;
                for (size_t i = 0; i < row.size(); ++i) row[i] = Number(array[i]);
            }
            else throw std::runtime_error("unknown mixer option");
        }
        catch (const std::exception& error) {
            throw std::runtime_error("in mixer option '" + key + "': " + error.what());
        }
    }
    if (table.count("left") != table.count("right")) throw std::runtime_error("mixer left and right must be supplied together");
    StereoMixer::Validate(config);
    return config;
}
}
