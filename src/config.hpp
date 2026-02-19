#ifndef MHWORLD_CUSTOM_FOV_CONFIG_HPP_INCLUDED
#define MHWORLD_CUSTOM_FOV_CONFIG_HPP_INCLUDED

#include "camera.hpp"

namespace config {

constexpr float default_fov = 53.0f;

struct Settings {
    float fov = default_fov;
    float distance = 1.0f;
    float height = 1.0f;
};

struct UserConfig {
    Settings hub_cam = {};
    Settings room_cam = {};
    Settings quest_cam = {};

    bool disable_room_shift = false;

    static
    auto from_file(string_view path) -> optional<UserConfig>;
    auto get_settings(camera::Context context) const -> Settings const&;
};

auto is_supported_version() -> bool;
void reload_config();
auto get_config() -> UserConfig const&;

} /* namespace config */

#endif /* include guard */
