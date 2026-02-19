#ifndef MHWORLD_CUSTOM_FOV_SHARED_HPP_INCLUDED
#define MHWORLD_CUSTOM_FOV_SHARED_HPP_INCLUDED

#include "loader.h"

#include <optional>
#include <string_view>

using std::optional, std::string_view;
using namespace loader;
using namespace std::literals;

constexpr auto PLUGIN_NAME = "CustomFOV"sv;
#define LOGLINE(level) LOG {level} << PLUGIN_NAME << ": "

struct Interval {
    float lower = 0.0f;
    float upper = 0.0f;
};

#endif /* include guard */
