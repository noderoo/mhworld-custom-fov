#include "camera.hpp"

#include "config.hpp"
#include "shared.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>

auto as_str(camera::Context context) -> string_view {
    switch (context) {
        case camera::Context::Hub: return "hub";
        case camera::Context::Room: return "room";
        case camera::Context::Quest: return "quest";
    }
    return "undefined";
}

namespace camera {

namespace /* unnamed */ {

enum class CameraID : uint32_t {
    Normal              =   0,
    Sprint              =   3,

    Combat              =  83,

    BaseHub             =  85,
    BaseHubSprint       =  86,

    LivingQuarters      = 118,
    PrivateQuarters     = 119,
    PrivateSuite        = 120,

    SurveyorSet         = 147,

    Seliana             = 252,
    SelianaSprint       = 253,
    SelianaHub          = 254,
    SelianaHubSprint    = 255,
    SelianaRoom         = 256,
};

auto operator<<(LOG &log, CameraID param_id) -> LOG& {
    log << static_cast<int>(param_id);
    return log;
}


constexpr
auto sets_hub_context(CameraID id) -> bool {
    constexpr auto set_hub_context_ids = std::to_array({
        CameraID::BaseHub,
        CameraID::BaseHubSprint,
        CameraID::Seliana,
        CameraID::SelianaSprint,
        CameraID::SelianaHub,
        CameraID::SelianaHubSprint,
    });
    return std::ranges::contains(set_hub_context_ids, id);
}

constexpr
auto sets_room_context(CameraID id) -> bool {
    constexpr auto set_room_context_ids = std::to_array({
        CameraID::LivingQuarters,
        CameraID::PrivateQuarters,
        CameraID::PrivateSuite,
        CameraID::SelianaRoom,
    });
    return std::ranges::contains(set_room_context_ids, id);
}

constexpr
auto sets_quest_context(CameraID id) -> bool {
    constexpr auto set_quest_context_ids = std::to_array({
        CameraID::Normal,
        CameraID::Sprint,
        CameraID::Combat,
    });
    return std::ranges::contains(set_quest_context_ids, id);
}

struct State {
    Context context = Context::Quest;
    CameraID camera_id = CameraID::Normal;

    auto update(CameraID new_camera_id) -> State const&;
};

auto g_state = State {};

auto State::update(CameraID new_camera_id) -> State const& {
    if (sets_hub_context(new_camera_id)) this->context = Context::Hub;
    if (sets_room_context(new_camera_id)) this->context = Context::Room;
    if (sets_quest_context(new_camera_id)) this->context = Context::Quest;
    this->camera_id = new_camera_id;
    return g_state;
}

constexpr auto PI = 3.1415927f;

auto proj_scale_from_fov(float fov) -> float {
    return std::tan(PI / 360 * fov);
}

auto fov_from_proj_scale(float proj_scale) -> float {
    return 360 / PI * std::atan(proj_scale);
}

struct Params {
    float fov      =  53.0f;
    float distance = 380.0f;
    float height   = 180.0f;
    float shift    =   0.0f;

    static auto from_context(Context context) -> Params;
    static auto from_memory(uintptr_t view_params) -> Params;
    void to_memory(uintptr_t view_params) const;
    auto adjust(State const& state) const -> Params;
};

constexpr auto default_hub_params = Params {
    .fov      =  53.0f,
    .distance = 350.0f,
    .height   = 170.0f,
    .shift    =   0.0f,
};

constexpr auto default_room_params = Params {
    .fov      =  51.0f,
    .distance = 260.0f,
    .height   = 160.0f,
    .shift    = -50.0f,
};

constexpr auto default_quest_params = Params {
    .fov      =  53.0f,
    .distance = 380.0f,
    .height   = 180.0f,
    .shift    =   0.0f,
};

auto Params::from_context(Context context) -> Params {
    switch (context) {
        case Context::Hub: return default_hub_params;
        case Context::Room: return default_room_params;
        case Context::Quest: return default_quest_params;
    }
    return {};
}

auto Params::from_memory(uintptr_t view_params) -> Params {
    auto params = Params {};
    params.fov      =  *reinterpret_cast<float*>(view_params + 0x20);
    params.distance = -*reinterpret_cast<float*>(view_params + 0x18);
    params.height   =  *reinterpret_cast<float*>(view_params + 0x14);
    params.shift    =  *reinterpret_cast<float*>(view_params + 0x10);
    return params;
}

void Params::to_memory(uintptr_t view_params) const {
    *reinterpret_cast<float*>(view_params + 0x20) =  this->fov;
    *reinterpret_cast<float*>(view_params + 0x18) = -this->distance;
    *reinterpret_cast<float*>(view_params + 0x14) =  this->height;
    *reinterpret_cast<float*>(view_params + 0x10) =  this->shift;
}

auto Params::adjust(State const& state) const -> Params {
    if (state.camera_id == CameraID::SurveyorSet) return *this; // don't touch surveyor set view
    auto const &config = config::get_config();
    auto const &settings = config.get_settings(state.context);
    auto const base_params = Params::from_context(state.context);

    auto const current_proj = proj_scale_from_fov(this->fov);
    auto const base_proj = proj_scale_from_fov(base_params.fov);
    auto const target_proj = proj_scale_from_fov(settings.fov);
    auto const adjusted_fov = fov_from_proj_scale(target_proj * current_proj / base_proj);

    auto const disable_shift = config.disable_room_shift && sets_room_context(state.camera_id);

    return Params {
        .fov = std::round(adjusted_fov),
        .distance = std::round(this->distance * settings.distance),
        .height = std::round(this->height * settings.height),
        .shift = disable_shift ? 0.0f : this->shift,
    };
}

void log_adjustment(State const& state, Params const& current_params, Params const& new_params) {
    auto line = std::stringstream {};
    auto log_param_adjustment = [&](string_view name, float old_value, float new_value) {
        if (!line.view().empty()) line << ", ";
        line << name << ' '  << std::fixed << std::setprecision(0) << old_value;
        if (old_value != new_value) line << " > " << std::setprecision(0) << new_value;
    };
    log_param_adjustment("fov", current_params.fov, new_params.fov);
    log_param_adjustment("distance", current_params.distance, new_params.distance);
    log_param_adjustment("height", current_params.height, new_params.height);
    log_param_adjustment("shift", current_params.shift, new_params.shift);
    LOGLINE(DEBUG) << as_str(state.context) << ' ' << std::setw(3) << state.camera_id << ' ' << line.view();
}

} /* unnamed namespace */

void update(uintptr_t camera_address) {
    auto const param_address = camera_address + 0x5d0;
    auto const current_params = Params::from_memory(param_address);
    auto const camera_id = *reinterpret_cast<CameraID*>(camera_address + 0x13b8);
    auto const& state = g_state.update(camera_id);
    auto const new_params = current_params.adjust(state);
    log_adjustment(state, current_params, new_params);
    new_params.to_memory(param_address);
}

} /* namespace camera */
