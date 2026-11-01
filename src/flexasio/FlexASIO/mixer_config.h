#pragma once

#include "mixer.h"
#include <toml/toml.h>

namespace flexasio {
    MixerConfig ParseMixerConfig(const toml::Table& table);
}
