#ifndef MHWORLD_CUSTOM_FOV_CAMERA_HPP_INCLUDED
#define MHWORLD_CUSTOM_FOV_CAMERA_HPP_INCLUDED

#include "shared.hpp"

#include <cstdint>

namespace camera {

enum class Context { Hub, Room, Quest };
void update(uintptr_t camera_address);

} /* namespace camera */

auto as_str(camera::Context context) -> string_view;

#endif /* include guard */
