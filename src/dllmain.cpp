#include "camera.hpp"
#include "config.hpp"
#include "shared.hpp"

#include "safetyhook.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <span>

namespace /* unnamed */ {

auto g_init_camera_hook = SafetyHookInline {};
auto g_update_camera_hook = SafetyHookInline {};

void hook_init_camera(uintptr_t camera, int camera_id) {
    config::reload_config();
    g_init_camera_hook.call(camera, camera_id);
    camera::update(camera);
}

void hook_update_camera(uintptr_t camera, uintptr_t view_param, uintptr_t interp_param, float param4) {
    config::reload_config();
    g_update_camera_hook.call(camera, view_param, interp_param, param4);
    camera::update(camera);
}

constexpr auto init_camera_bytes = std::to_array<uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
    0x48, 0x83, 0xEC, 0x20,
});

constexpr auto update_camera_bytes = std::to_array<uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9,
});

auto check_bytes(uintptr_t target, std::span<uint8_t const> bytes) -> bool {
    if (!std::equal(bytes.begin(), bytes.end(), reinterpret_cast<uint8_t const*>(target))) {
        LOGLINE(ERR) << "Function at 0x" << std::hex << target << " does not match expected bytes!";
        return false;
    }
    return true;
}

struct Targets {
    uintptr_t init_camera_addr;
    uintptr_t update_camera_addr;

    auto check() const -> bool {
        return check_bytes(this->init_camera_addr, init_camera_bytes)
            && check_bytes(this->update_camera_addr, update_camera_bytes);
    }
};

constexpr auto targets_421810 = Targets {
    .init_camera_addr = 0x141fa0fe0,
    .update_camera_addr = 0x141fa6be0
};

auto get_targets() -> optional<Targets> {
    if (!config::is_supported_version()) {
        LOGLINE(ERR) << "Unsupported game version!";
        return std::nullopt;
    }
    auto const targets = targets_421810;
    if (!targets.check()) return std::nullopt;
    return targets;
}

void log_safetyhook_allocator_error(std::stringstream &line, safetyhook::Allocator::Error const& error) {
    line << "An error occurred when allocating memory: ";
    switch (error) {
        case safetyhook::Allocator::Error::BAD_VIRTUAL_ALLOC:
            line << "VirtualAlloc failed.";
            break;
        case safetyhook::Allocator::Error::NO_MEMORY_IN_RANGE:
            line << "No memory in range.";
            break;
        default:
            line << "Unknown allocator error.";
            break;
    }
}

void log_safetyhook_error(SafetyHookInline::Error const& error) {
    auto line = std::stringstream {};
    auto log_instruction_pointer = [&](uint8_t *ip) {
        line << " (IP @ 0x" << std::hex << reinterpret_cast<uintptr_t>(ip) << ')';
    };
    switch (error.type) {
        case SafetyHookInline::Error::BAD_ALLOCATION:
            log_safetyhook_allocator_error(line, error.allocator_error);
            break;

        case SafetyHookInline::Error::FAILED_TO_DECODE_INSTRUCTION:
            line << "Failed to decode an instruction.";
            log_instruction_pointer(error.ip);
            break;

        case SafetyHookInline::Error::SHORT_JUMP_IN_TRAMPOLINE:
            line << "The trampoline contains a short jump.";
            log_instruction_pointer(error.ip);
            break;

        case SafetyHookInline::Error::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE:
            line << "An IP - relative instruction is out of range.";
            log_instruction_pointer(error.ip);
            break;

        case SafetyHookInline::Error::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE:
            line << "An unsupported instruction was found in the trampoline.";
            log_instruction_pointer(error.ip);
            break;

        case SafetyHookInline::Error::FAILED_TO_UNPROTECT:
            line << "Failed to unprotect memory.";
            log_instruction_pointer(error.ip);
            break;

        case SafetyHookInline::Error::NOT_ENOUGH_SPACE:
            line << "Not enough space to create the hook.";
            log_instruction_pointer(error.ip);
            break;

        default:
            line << "Unknown safetyhook error.";
            break;
    }
    LOGLINE(ERR) << line.view();
}

auto create_hook(uintptr_t target, void* destination, SafetyHookInline &hook) -> bool {
    auto result = SafetyHookInline::create(target, destination);
    if (!result) {
        LOGLINE(ERR) << "Failed to create hook for function at 0x" << std::hex << target << '!';
        log_safetyhook_error(result.error());
        return false;
    }
    hook = std::move(*result);
    return true;
}

auto create_hooks() -> bool {
    auto const targets = get_targets();
    if (!targets) return false;
    auto const [init_camera_addr, update_camera_addr] = *targets;
    return create_hook(init_camera_addr, hook_init_camera, g_init_camera_hook)
        && create_hook(update_camera_addr, hook_update_camera, g_update_camera_hook);
}

void reset_hooks() {
    LOGLINE(INFO) << "Resetting hooks...";
    g_init_camera_hook.reset();
    g_update_camera_hook.reset();
}

} /* unnamed namespace */

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            LOGLINE(INFO) << "Attaching plugin...";
            if (!create_hooks()) return false;
            LOGLINE(INFO) << "Success!";
            break;
        }
        case DLL_PROCESS_DETACH: {
            reset_hooks();
            LOGLINE(INFO) << "Plugin detached.";
            break;
        }
    }
    return true;
}
