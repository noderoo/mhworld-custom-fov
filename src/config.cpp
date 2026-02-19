#include "config.hpp"

#include "shared.hpp"

#define TOML_EXCEPTIONS 0
#include "toml.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>

using camera::Context;

namespace config {

namespace /* unnamed */ {

struct Trace {
    Trace const* parent;
    string_view key;
};

auto operator<<(LOG& log, Trace const& trace) -> LOG&
{
    if (trace.parent) log << *trace.parent << '.';
    log << trace.key;
    return log;
}

template <typename T> constexpr auto type_name() -> string_view = delete;
template <> constexpr auto type_name<bool>() -> string_view { return "boolean"; }
template <> constexpr auto type_name<float>() -> string_view { return "floating-point"; }

template <typename T>
auto read_value(toml::table const& table, string_view key, Trace const* trace)
  -> optional<T>
{
    auto const node_trace = Trace {trace, key};
    auto const node_view = table[key];
    if (!node_view) return std::nullopt;
    auto const value = node_view.value<T>();
    if (!value) {
        auto const expected_type = type_name<T>();
        LOGLINE(ERR) << "Expected " << node_trace << " to be a " << expected_type
            << ", but got a " << node_view.type() << '!';
        return std::nullopt;
    }
    return *value;
}

template <typename T>
auto from_table(toml::table const& table, Trace const* trace, T const& base) -> T = delete;

template <typename T>
auto from_table_at_key(toml::table const& table, string_view key, Trace const* trace, T const& defaults)
  -> T
{
    auto const node_trace = Trace {trace, key};
    auto const node_view = table[key];
    if (!node_view) return defaults;
    if (!node_view.is_table()) {
        auto const expected_type = "table"sv;
        LOGLINE(ERR) << "Expected " << node_trace << " to be a " << expected_type
            << ", but got a " << node_view.type() << '!';
        return defaults;
    }
    return from_table<T>(*node_view.as_table(), &node_trace, defaults);
}

void warn_unknown_keys(toml::table const& table, std::span<string_view const> expected_keys, Trace const* trace) {
    for (auto&& [key, value] : table) {
        if (!std::ranges::contains(expected_keys, key)) {
            auto key_trace = Trace {trace, key};
            LOGLINE(WARN) << "Unknown key " << key_trace << " will be ignored.";
        }
    }
}

auto clamp_fov(float value) -> float {
    constexpr auto fov_range = Interval { .lower = 30.0f, .upper = 120.0f };
    auto const clamped = std::clamp(value, fov_range.lower, fov_range.upper);
    if (clamped != value) LOGLINE(WARN) << "FOV clamped to range [" << fov_range.lower << ", " << fov_range.upper << "].";
    return clamped;
}

template <>
auto from_table<Settings>(toml::table const& table, Trace const *trace, Settings const& defaults) -> Settings {
    constexpr auto expected_keys = std::to_array<string_view>({"fov", "distance", "height", "shift"});
    if (trace != nullptr) warn_unknown_keys(table, expected_keys, trace);
    return Settings {
        .fov      = read_value<float>(table, "fov", trace).transform(clamp_fov).value_or(defaults.fov),
        .distance = read_value<float>(table, "distance", trace).value_or(defaults.distance),
        .height   = read_value<float>(table, "height", trace).value_or(defaults.height),
    };
}

auto g_config = UserConfig {};
auto g_config_last_write_time = std::optional<std::filesystem::file_time_type> {};

auto get_config_path(string_view version) -> std::optional<string_view> {
    if (version.starts_with("314")) return "ICE/ntPC/plugins/CustomFOV.toml"sv;
    if (version.starts_with("421")) return "nativePC/plugins/CustomFOV.toml"sv;
    return std::nullopt;
}

} /* unnamed namespace */

auto is_supported_version() -> bool {
    return get_config_path(GameVersion).has_value();
}

auto UserConfig::from_file(string_view path) -> optional<UserConfig> {
    LOGLINE(DEBUG) << "Parsing config file '" << path << "'...";
    auto const parse_result = toml::parse_file(path);
    if (parse_result.failed()) {
        auto const& error = parse_result.error();
        LOGLINE(ERR) << error.description();
        LOGLINE(ERR) << "^ occured on " << error.source();
        return std::nullopt;
    }
    auto const& table = parse_result.table();
    constexpr auto expected_keys = std::to_array<string_view>({
        "fov", "distance", "height", "hub", "room", "quest", "disable_room_shift"
    });
    warn_unknown_keys(table, expected_keys, nullptr);

    auto const global_cam = from_table<Settings>(table, nullptr, Settings {});
    auto resolve_settings = [&](Context context) {
        return from_table_at_key<Settings>(table, as_str(context), nullptr, global_cam);
    };

    return UserConfig {
        .hub_cam = resolve_settings(Context::Hub),
        .room_cam = resolve_settings(Context::Room),
        .quest_cam = resolve_settings(Context::Quest),
        .disable_room_shift = read_value<bool>(table, "disable_room_shift", nullptr)
            .value_or(false),
    };
}

auto UserConfig::get_settings(Context context) const -> Settings const& {
    switch (context) {
        case Context::Hub:   return g_config.hub_cam;
        case Context::Room:  return g_config.room_cam;
        case Context::Quest: return g_config.quest_cam;
    }
    return g_config.hub_cam;
}

auto get_config() -> UserConfig const& {
    return g_config;
}

void reload_config() {
    auto const path = get_config_path(GameVersion);
    if (!path.has_value()) return;

    auto error_code = std::error_code {};
    auto const last_write_time = std::filesystem::last_write_time(*path, error_code);
    if (error_code) return; // check again next time

    if (g_config_last_write_time.has_value() && last_write_time <= *g_config_last_write_time) return;

    auto const config_result = UserConfig::from_file(*path);
    if (config_result.has_value()) {
        g_config = *config_result;
    } else {
        LOGLINE(WARN) << "Keeping existing settings.";
    }
    g_config_last_write_time = last_write_time;
}

} /* namespace config */
