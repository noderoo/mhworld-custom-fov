#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define TOML_EXCEPTIONS 0
#include "toml.hpp"
#include "loader.h"
#include "MinHook.h"

#include <algorithm> // clamp
#include <bit> // bit_cast
#include <cmath> // tan, atan
#include <iomanip> // format specifiers
#include <span>
#include <vector>

using namespace loader;

constexpr auto plugin_name = std::string_view {"CustomFOV"};
constexpr auto log_prefix = std::string_view {"CustomFOV: "};

constexpr auto vanilla_fov = 53.0f;
constexpr auto vanilla_distance = 380.0f;
constexpr auto vanilla_height = 180.0f;

constexpr auto pi = 3.1415927f;

inline auto gradient_from_fov(float fov) -> float { return std::tan(pi * fov / 360); }
inline auto fov_from_gradient(float gradient) -> float { return 360 / pi * std::atan(gradient); }

constexpr auto vanilla_gradient = 0.4985816f; // gradient_from_fov(vanilla_fov)

constexpr auto min_fov =  10.0f;
constexpr auto max_fov = 130.0f;

auto clamp_fov(float fov) -> float
{
    auto const clamped_fov = std::clamp(fov, min_fov, max_fov);
    if (clamped_fov != fov) LOG(WARN) << log_prefix << "FOV is not in range [" << min_fov << ", " << max_fov << "]!";
    return clamped_fov;
}

struct Tweak
{
    float target;
    bool effects;
};

struct Config
{
    bool live_reload = true;

    Tweak fov      = {vanilla_fov,      true};
    Tweak distance = {vanilla_distance, true};
    Tweak height   = {vanilla_height,   true};
    Tweak shift    = {0.0f,             true};
};

static auto g_config = Config {};

auto get_config_path(std::string_view version) -> std::optional<std::string_view>
{
    if (version.starts_with("314")) return "ICE/ntPC/plugins/CustomFOV.toml";
    if (version.starts_with("421")) return "nativePC/plugins/CustomFOV.toml";
    return std::nullopt;
}

template <typename T>
auto parse_option(toml::table const &table, std::string_view path) -> std::optional<T>
{
    auto const node = table.at_path(path);
    if (!node)
    {
        LOG(ERR) << log_prefix << "Could not find option '" << path << "'!";
        return std::nullopt;
    }

    auto const value = node.value<T>();
    if (!value.has_value())
    {
        LOG(ERR) << log_prefix << "Option '" << path << "' has invalid type!";
        return std::nullopt;
    }

    return *value;
}

auto parse_tweak(toml::table const &table, std::string_view name, Tweak const &defaults)
{
    return Tweak
    {
        .target = parse_option<float>(table, std::string {name} + ".target").value_or(defaults.target),
        .effects = parse_option<bool>(table, std::string {name} + ".effects").value_or(defaults.effects),
    };
}

auto parse_config(std::string_view path) -> Config
{
    auto config = Config {};

    LOG(DEBUG) << log_prefix << "Parsing config file \"" << path << "\"...";
    auto const parse_result = toml::parse_file(path);
    if (parse_result.failed())
    {
        LOG(ERR) << log_prefix << "Failed to parse config file! " << parse_result.error().description();
        return config;
    }

    auto const &table = parse_result.table();

    config.live_reload = parse_option<bool>(table, "live_reload").value_or(config.live_reload);

    config.fov      = parse_tweak(table, "fov",      config.fov);
    config.distance = parse_tweak(table, "distance", config.distance);
    config.height   = parse_tweak(table, "height",   config.height);
    config.shift    = parse_tweak(table, "shift",    config.shift);
 
    return config;
}

void load_config()
{
    auto const path = get_config_path(GameVersion);
    if (path.has_value()) g_config = parse_config(path.value());
    if (!g_config.live_reload) LOG(INFO) << log_prefix << "Live reload was disabled.";
}

struct Camera { float shift, height, distance, fov; };

auto read_camera(uintptr_t view_params) -> Camera
{
    return Camera
    {
        .shift    =  *std::bit_cast<float *>(view_params + 0x10),
        .height   =  *std::bit_cast<float *>(view_params + 0x14),
        .distance = -*std::bit_cast<float *>(view_params + 0x18), // distances are negative
        .fov      =  *std::bit_cast<float *>(view_params + 0x20),
    };
}

void write_camera(uintptr_t view_params, Camera const &camera)
{
    *std::bit_cast<float *>(view_params + 0x10) =  camera.shift;
    *std::bit_cast<float *>(view_params + 0x14) =  camera.height;
    *std::bit_cast<float *>(view_params + 0x18) = -camera.distance; // distances are negative
    *std::bit_cast<float *>(view_params + 0x20) =  camera.fov;
}

auto tweak_default(float value, Tweak const &tweak, float reference) -> float
{
    auto const effect = tweak.effects ? value / reference : 1;
    return tweak.target * effect;
}

auto tweak_fov(float fov, Tweak const &tweak) -> float
{
    auto const effect = tweak.effects ? gradient_from_fov(fov) / vanilla_gradient : 1;
    return clamp_fov(fov_from_gradient(effect * gradient_from_fov(tweak.target)));
}

auto tweak_shift(float shift, Tweak const &tweak) -> float
{
    auto const effect = tweak.effects ? shift : 0;
    auto const sign = std::signbit(tweak.target) ? -1 : 1;
    return tweak.target ? sign * std::hypot(tweak.target, effect) : effect;
}

auto tweak_camera(Camera const &camera, Config const &config) -> Camera
{
    return Camera
    {
        .shift = tweak_shift(camera.shift, config.shift),
        .height = tweak_default(camera.height, config.height, vanilla_height),
        .distance = tweak_default(camera.distance, config.distance, vanilla_distance),
        .fov = tweak_fov(camera.fov, config.fov),
    };
}

void log_camera_change(Camera const &old_camera, Camera const &new_camera)
{
    LOG(INFO) << log_prefix << std::fixed 
        << "fov"        << std::setw(3) << std::setprecision(0) << old_camera.fov
        << " >"         << std::setw(6) << std::setprecision(1) << new_camera.fov
        << ", distance" << std::setw(4) << std::setprecision(0) << old_camera.distance
        << " >"         << std::setw(7) << std::setprecision(1) << new_camera.distance
        << ", height"   << std::setw(4) << std::setprecision(0) << old_camera.height
        << " >"         << std::setw(6) << std::setprecision(1) << new_camera.height
        << ", shift"    << std::setw(4) << std::setprecision(0) << old_camera.shift
        << " >"         << std::setw(7) << std::setprecision(1) << new_camera.shift;
}

void handle_camera_change(uintptr_t player_camera)
{
    auto const view_params = player_camera + 0x5d0;
    auto const camera = read_camera(view_params);
    auto const new_camera = tweak_camera(camera, g_config);
    write_camera(view_params, new_camera);
    log_camera_change(camera, new_camera);
}

using init_camera_fn = void (*)(uintptr_t, int);
auto init_camera_original = init_camera_fn {nullptr};

void init_camera_hook(uintptr_t player_camera, int camera_id)
{
    if (g_config.live_reload) load_config();
    init_camera_original(player_camera, camera_id);
    handle_camera_change(player_camera);
}

using set_camera_fn = void (*)(uintptr_t, uintptr_t, uintptr_t, float);
auto set_camera_original = set_camera_fn {nullptr};

void set_camera_hook(uintptr_t player_camera, uintptr_t view_param, uintptr_t interpolation_param, float param4)
{
    set_camera_original(player_camera, view_param, interpolation_param, param4);
    handle_camera_change(player_camera);
}

template <typename T>
bool queue_hook(void *target, void *detour, T **original)
{
    LOG(DEBUG) << log_prefix << "Creating hook for 0x" << target;
    if (auto const status = MH_CreateHook(target, detour, reinterpret_cast<void **>(original)); status != MH_OK)
    {
        LOG(ERR) << log_prefix << "Failed to create hook! (" << MH_StatusToString(status) << ')';
        return false;
    }

    LOG(DEBUG) << log_prefix << "Queueing hook...";
    if (auto const status = MH_QueueEnableHook(target); status != MH_OK)
    {
        LOG(ERR) << log_prefix << "Failed to queue hook! (" << MH_StatusToString(status) << ')';
        return false;
    }

    return true;
}

auto check_bytes(void *address, std::span<std::uint8_t const> bytes) -> bool
{
    return std::equal(std::cbegin(bytes), std::cend(bytes), reinterpret_cast<std::uint8_t *>(address));
}

auto attach_plugin() -> bool
{
    auto const config_path = get_config_path(GameVersion);
    if (!config_path.has_value())
    {
        LOG(ERR) << log_prefix << "Unsupported game version!";
        return false;
    }

    LOG(DEBUG) << log_prefix << "Initializing MinHook...";
    if (auto const status = MH_Initialize(); status != MH_OK)
    {
        LOG(ERR) << log_prefix << "Failed to initialize MinHook! (" << MH_StatusToString(status) << ')';
        return false;
    }

    auto const set_camera = std::bit_cast<void *>(0x141fa6be0);
    auto const set_camera_expected_bytes = std::vector<std::uint8_t>
    {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9,
    };
    if (!check_bytes(set_camera, set_camera_expected_bytes))
    {
        LOG(ERR) << log_prefix << "Function at 0x" << set_camera << " does not match expected bytes!";
        return false;
    }
    if (!queue_hook(set_camera, set_camera_hook, &set_camera_original)) return false;
    
    auto const init_camera = std::bit_cast<void *>(0x141fa0fe0);
    auto const init_camera_expected_bytes = std::vector<std::uint8_t>
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
        0x48, 0x83, 0xEC, 0x20
    };
    if (!check_bytes(init_camera, init_camera_expected_bytes))
    {
        LOG(ERR) << log_prefix << "Function at 0x" << init_camera << " does not match expected bytes!";
        return false;
    }
    if (!queue_hook(init_camera, init_camera_hook, &init_camera_original)) return false;

    if (auto const status = MH_SetThreadFreezeMethod(MH_FREEZE_METHOD_FAST_UNDOCUMENTED); status != MH_OK)
    {
        LOG(WARN) << log_prefix << "Failed to set thread freeze method! (" << MH_StatusToString(status) << ')';
        return false;
    }

    switch (auto const method = MH_GetThreadFreezeMethod())
    {
        case MH_FREEZE_METHOD_ORIGINAL:
            LOG(DEBUG) << log_prefix << "Using original thread freeze method.";
            break;

        case MH_FREEZE_METHOD_FAST_UNDOCUMENTED:
            LOG(DEBUG) << log_prefix << "Using fast thread freeze method.";
            break;

        case MH_FREEZE_METHOD_NONE_UNSAFE:
            LOG(DEBUG) << log_prefix << "Not freezing threads.";
            break;
    }

    LOG(DEBUG) << log_prefix << "Applying queued hooks...";
    if (auto const status = MH_ApplyQueued(); status != MH_OK)
    {
        LOG(ERR) << log_prefix << "Failed to apply queued hooks! (" << MH_StatusToString(status) << ')';
        return false;
    }

    return true;
}

auto detach_plugin() -> bool
{
    LOG(DEBUG) << log_prefix << "Uninitializing MinHook...";
    if (auto const status = MH_Uninitialize(); status != MH_OK && status != MH_ERROR_NOT_INITIALIZED)
    {
        LOG(ERR) << log_prefix << "Failed to uninitialize MinHook! (" << MH_StatusToString(status) << ')';
        return false;
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            LOG(INFO) << log_prefix << "Attaching plugin...";
            if (!attach_plugin()) return false;
            LOG(INFO) << log_prefix << "Success!";
            break;
        
        case DLL_PROCESS_DETACH:
            LOG(INFO) << log_prefix << "Detaching plugin...";
            if (!detach_plugin()) return false;
            LOG(INFO) << log_prefix << "Success!";
            break;
    }

    return true;
}
