/**
 * @file src/platform/linux/stream_display_policy.cpp
 * @brief Linux stream display policy — facade over stream_path registry.
 */

#include "stream_display_policy.h"

#include "stream_path.h"
#include "src/config.h"
#include "virtual_display.h"

#include <cctype>

namespace stream_display_policy {

  namespace {

    std::string to_lower_copy(std::string_view value) {
      std::string out;
      out.reserve(value.size());
      for (unsigned char ch : value) {
        out.push_back(static_cast<char>(std::tolower(ch)));
      }
      return out;
    }

    mode_e mode_from_path_id(std::string_view id) {
      const auto key = to_lower_copy(id);
      if (key == stream_path::k_headless_stream) {
        return mode_e::HEADLESS;
      }
      if (key == stream_path::k_windowed_stream) {
        return mode_e::GPU_NATIVE_STREAM;
      }
      if (key == stream_path::k_host_virtual_display) {
        return mode_e::HOST_VIRTUAL_DISPLAY;
      }
      if (key == stream_path::k_gamescope_stream) {
        return mode_e::GAMESCOPE_STREAM;
      }
      if (key == stream_path::k_family_isolated) {
        return mode_e::WINDOWED_STREAM;  // nested labwc under host
      }
      if (key == stream_path::k_headless_evdi) {
        return mode_e::HOST_VIRTUAL_DISPLAY;
      }
      if (key == stream_path::k_headless_dongle) {
        // Host desktop + topology swap (not a software virtual display create path).
        return mode_e::DESKTOP;
      }
      return mode_e::DESKTOP;
    }

    private_runtime_e runtime_from_kind(stream_path::runtime_kind_e kind) {
      switch (kind) {
        case stream_path::runtime_kind_e::LABWC:
          return private_runtime_e::LABWC;
        case stream_path::runtime_kind_e::GAMESCOPE:
          return private_runtime_e::GAMESCOPE;
        case stream_path::runtime_kind_e::NONE:
        default:
          return private_runtime_e::NONE;
      }
    }

    resolved_t from_path_resolved(const stream_path::resolved_t &path) {
      resolved_t result;
      result.mode = mode_from_path_id(path.selection);
      result.selection = path.selection;
      result.label = path.label;
      result.reason = path.reason;
      result.private_runtime = runtime_from_kind(path.runtime);
      result.available = path.available;
      result.unavailable_reason = path.unavailable_reason;
      result.requested_headless = path.requested_headless;
      result.effective_headless = path.effective_headless;
      result.use_private_runtime = path.use_private_runtime;
      result.use_cage_runtime = path.use_cage_runtime;
      result.use_host_virtual_display = path.use_host_virtual_display;
      result.prefer_gpu_native_capture = path.prefer_gpu_native_capture;
      result.should_defer_encoder_probe = path.should_defer_encoder_probe;
      result.should_probe_against_runtime = path.should_probe_against_runtime;
      result.backend_name = path.backend_name;
      return result;
    }

    std::string configured_selection_id() {
      auto &linux_display = config::video.linux_display;
      if (!linux_display.stream_mode.empty()) {
        if (stream_path::find(linux_display.stream_mode)) {
          return to_lower_copy(linux_display.stream_mode);
        }
      }
      return selection_from_legacy_booleans({
        linux_display.headless_mode,
        linux_display.use_cage_compositor,
        linux_display.prefer_gpu_native_capture,
      });
    }

  }  // namespace

  std::optional<mode_e> parse_selection(std::string_view selection) {
    if (!stream_path::find(selection) && to_lower_copy(selection) != k_desktop_display) {
      // Allow known policy-only ids even if not in registry (should not happen).
      const auto key = to_lower_copy(selection);
      if (key != k_headless_stream && key != k_windowed_stream &&
          key != k_host_virtual_display && key != k_gamescope_stream &&
          key != k_desktop_display) {
        return std::nullopt;
      }
    }
    return mode_from_path_id(selection);
  }

  std::string_view selection_for_mode(mode_e mode) {
    switch (mode) {
      case mode_e::HEADLESS:
        return k_headless_stream;
      case mode_e::WINDOWED_STREAM:
      case mode_e::GPU_NATIVE_STREAM:
        return k_windowed_stream;
      case mode_e::HOST_VIRTUAL_DISPLAY:
        return k_host_virtual_display;
      case mode_e::GAMESCOPE_STREAM:
        return k_gamescope_stream;
      case mode_e::DESKTOP:
      default:
        return k_desktop_display;
    }
  }

  std::string label_for_selection(std::string_view selection) {
    if (const auto *path = stream_path::find(selection)) {
      return std::string {path->label};
    }
    return {};
  }

  std::string reason_for_selection(std::string_view selection, bool virtual_display_available) {
    if (const auto *path = stream_path::find(selection)) {
      if (path->id == stream_path::k_host_virtual_display && !virtual_display_available) {
        return "Polaris requested a host virtual display, but no backend is currently available.";
      }
      return std::string {path->reason};
    }
    return "Polaris will mirror the current desktop session.";
  }

  bool selection_available(std::string_view selection) {
    if (const auto *path = stream_path::find(selection)) {
      return path->available;
    }
    return false;
  }

  std::string selection_unavailable_reason(std::string_view selection) {
    if (const auto *path = stream_path::find(selection)) {
      return std::string {path->unavailable_reason};
    }
    return "Unknown stream display mode.";
  }

  std::string selection_from_legacy_booleans(const legacy_booleans_t &booleans) {
    if (!booleans.headless_mode) {
      if (booleans.use_cage_compositor) {
        return std::string {k_windowed_stream};
      }
      return std::string {k_desktop_display};
    }
    if (!booleans.use_cage_compositor) {
      return std::string {k_host_virtual_display};
    }
    if (booleans.prefer_gpu_native_capture) {
      return std::string {k_windowed_stream};
    }
    return std::string {k_headless_stream};
  }

  legacy_booleans_t legacy_booleans_for_selection(std::string_view selection) {
    legacy_booleans_t booleans;
    const auto key = to_lower_copy(selection);
    if (key == k_headless_stream || key == stream_path::k_family_isolated) {
      booleans.headless_mode = key != stream_path::k_family_isolated;
      booleans.use_cage_compositor = true;
      booleans.prefer_gpu_native_capture = false;
      if (key == stream_path::k_family_isolated) {
        booleans.headless_mode = false;  // nested under host Wayland
        booleans.use_cage_compositor = true;
      }
    }
    else if (key == k_windowed_stream) {
      booleans.headless_mode = true;
      booleans.use_cage_compositor = true;
      booleans.prefer_gpu_native_capture = true;
    }
    else if (key == k_host_virtual_display ||
             key == stream_path::k_headless_evdi ||
             key == stream_path::k_headless_dongle) {
      // Dongle/EVDI: host desktop path with topology swap — not private labwc.
      booleans.headless_mode = true;
      booleans.use_cage_compositor = false;
      booleans.prefer_gpu_native_capture = false;
    }
    else if (key == k_gamescope_stream) {
      booleans.headless_mode = true;
      booleans.use_cage_compositor = false;
      booleans.prefer_gpu_native_capture = false;
    }
    else {
      booleans.headless_mode = false;
      booleans.use_cage_compositor = false;
      booleans.prefer_gpu_native_capture = false;
    }
    return booleans;
  }

  private_runtime_e parse_private_runtime(std::string_view value) {
    switch (stream_path::parse_runtime_kind(value)) {
      case stream_path::runtime_kind_e::LABWC:
        return private_runtime_e::LABWC;
      case stream_path::runtime_kind_e::GAMESCOPE:
        return private_runtime_e::GAMESCOPE;
      case stream_path::runtime_kind_e::NONE:
      default:
        // Empty private_runtime historically meant labwc for cage paths.
        if (value.empty()) {
          return private_runtime_e::LABWC;
        }
        return private_runtime_e::NONE;
    }
  }

  std::string_view private_runtime_id(private_runtime_e runtime) {
    switch (runtime) {
      case private_runtime_e::GAMESCOPE:
        return k_runtime_gamescope;
      case private_runtime_e::LABWC:
        return k_runtime_labwc;
      case private_runtime_e::NONE:
      default:
        return {};
    }
  }

  resolved_t resolve(const input_t &input) {
    const auto selection = configured_selection_id();
    const auto *path = stream_path::find(selection);
    stream_path::descriptor_t desc {};
    if (path) {
      desc = *path;
    }
    else {
      desc = *stream_path::find(stream_path::k_desktop_display);
    }

    // Edge: legacy !headless + cage without stream_mode → windowed private labwc.
    if (config::video.linux_display.stream_mode.empty() &&
        !config::video.linux_display.headless_mode &&
        config::video.linux_display.use_cage_compositor) {
      if (const auto *windowed = stream_path::find(stream_path::k_windowed_stream)) {
        desc = *windowed;
        desc.request_headless = false;
      }
    }

    auto caps = stream_path::probe_host_capabilities();
    if (input.virtual_display_available) {
      caps.virtual_display_available = true;
    }

    auto path_resolved = stream_path::resolve_path(
      desc,
      caps,
      input.active_encoder_requires_gpu_native_capture,
      input.runtime_gpu_native_override_active
    );
    return from_path_resolved(path_resolved);
  }

  resolved_t resolve_current(bool active_encoder_requires_gpu_native_capture,
                             bool runtime_gpu_native_override_active) {
    return resolve(input_t {
      virtual_display::is_available(),
      active_encoder_requires_gpu_native_capture,
      runtime_gpu_native_override_active,
    });
  }

  resolved_t resolve_effective(const input_t &input,
                               bool streaming,
                               bool session_uses_virtual_display,
                               bool runtime_effective_headless) {
    if (!streaming) {
      return resolve(input);
    }

    auto configured = resolve(input);
    if (input.runtime_gpu_native_override_active) {
      configured.mode = mode_e::GPU_NATIVE_STREAM;
      configured.selection = std::string {k_windowed_stream};
      configured.label = label_for_selection(k_windowed_stream);
      configured.effective_headless = false;
      configured.prefer_gpu_native_capture = true;
      configured.backend_name = "labwc";
      return configured;
    }
    if (session_uses_virtual_display) {
      if (const auto *path = stream_path::find(stream_path::k_host_virtual_display)) {
        auto caps = stream_path::probe_host_capabilities();
        caps.virtual_display_available = input.virtual_display_available;
        return from_path_resolved(stream_path::resolve_path(*path, caps));
      }
    }
    if (configured.selection == k_windowed_stream && runtime_effective_headless) {
      return configured;
    }
    if (runtime_effective_headless && configured.use_cage_runtime) {
      configured.mode = mode_e::HEADLESS;
      configured.selection = std::string {k_headless_stream};
      configured.label = label_for_selection(k_headless_stream);
      configured.effective_headless = true;
      return configured;
    }
    if (configured.use_private_runtime || configured.use_cage_runtime) {
      return configured;
    }
    return configured;
  }

  bool apply_selection(std::string_view selection, std::string &error) {
    const auto key = to_lower_copy(selection);
    if (!parse_selection(key)) {
      error = "stream_display_mode must be a known stream path id (see /client-settings modes)";
      return false;
    }
    if (!selection_available(key)) {
      error = selection_unavailable_reason(key);
      return false;
    }

    auto &linux_display = config::video.linux_display;

    // Dongle path needs outputs configured before we commit.
    if (key == stream_path::k_headless_dongle) {
      if (linux_display.streaming_output.empty() || linux_display.primary_output.empty()) {
        error = "headless_dongle requires linux_streaming_output (dongle) and linux_primary_output (real panel) in config";
        return false;
      }
      if (linux_display.streaming_output == linux_display.primary_output) {
        error = "headless_dongle needs distinct streaming and primary outputs";
        return false;
      }
      linux_display.auto_manage_displays = true;
      if (linux_display.headless_swap_mode.empty()) {
        linux_display.headless_swap_mode = "privacy";
      }
      // Prefer KMS capture of the dongle connector by name.
      if (config::video.capture.empty() || config::video.capture == "portal") {
        config::video.capture = "kms";
      }
      if (config::video.output_name.empty()) {
        config::video.output_name = linux_display.streaming_output;
      }
    }

    const auto booleans = legacy_booleans_for_selection(key);
    linux_display.stream_mode = key;
    linux_display.headless_mode = booleans.headless_mode;
    linux_display.use_cage_compositor = booleans.use_cage_compositor;
    linux_display.prefer_gpu_native_capture = booleans.prefer_gpu_native_capture;

    if (const auto *path = stream_path::find(key)) {
      linux_display.private_runtime = std::string {stream_path::runtime_kind_id(path->runtime)};
      if (linux_display.private_runtime.empty() && booleans.use_cage_compositor) {
        linux_display.private_runtime = std::string {k_runtime_labwc};
      }
    }
    else if (booleans.use_cage_compositor) {
      linux_display.private_runtime = std::string {k_runtime_labwc};
    }

    return true;
  }

  void normalize_config_from_load() {
    auto &linux_display = config::video.linux_display;

    if (!linux_display.stream_mode.empty()) {
      if (const auto *path = stream_path::find(linux_display.stream_mode)) {
        const auto booleans = legacy_booleans_for_selection(path->id);
        linux_display.headless_mode = booleans.headless_mode;
        linux_display.use_cage_compositor = booleans.use_cage_compositor;
        linux_display.prefer_gpu_native_capture = booleans.prefer_gpu_native_capture;
        if (path->runtime == stream_path::runtime_kind_e::LABWC) {
          linux_display.private_runtime = std::string {k_runtime_labwc};
        }
        else if (path->runtime == stream_path::runtime_kind_e::GAMESCOPE) {
          linux_display.private_runtime = std::string {k_runtime_gamescope};
        }
        return;
      }
      linux_display.stream_mode.clear();
    }

    linux_display.stream_mode = selection_from_legacy_booleans({
      linux_display.headless_mode,
      linux_display.use_cage_compositor,
      linux_display.prefer_gpu_native_capture,
    });

    if (linux_display.private_runtime.empty()) {
      linux_display.private_runtime = std::string {k_runtime_labwc};
    }
  }

  std::vector<mode_option_t> mode_options(bool virtual_display_available) {
    auto caps = stream_path::probe_host_capabilities();
    caps.virtual_display_available = virtual_display_available;
    std::vector<mode_option_t> options;
    for (const auto &path : stream_path::options_for_host(caps)) {
      mode_option_t option;
      option.value = std::string {path.id};
      option.label = std::string {path.label};
      option.reason = reason_for_selection(path.id, virtual_display_available);
      option.available = path.available;
      option.unavailable_reason = std::string {path.unavailable_reason};
      option.group = std::string {path.group};
      option.runtime = std::string {stream_path::runtime_kind_id(path.runtime)};
      option.capture = std::string {stream_path::capture_kind_id(path.capture)};
      option.topology = std::string {stream_path::topology_kind_id(path.topology)};
      options.push_back(std::move(option));
    }
    return options;
  }

  std::vector<std::string> allowed_launch_modes(bool virtual_display_available,
                                                bool include_unavailable) {
    std::vector<std::string> modes;
    for (const auto &path : stream_path::registry()) {
      if (!path.available && !include_unavailable) {
        continue;
      }
      // Launch contract only lists primary user paths by default.
      if (path.group == "experimental" && !include_unavailable) {
        continue;
      }
      if (path.id == stream_path::k_host_virtual_display && !virtual_display_available) {
        continue;
      }
      if (!path.available) {
        continue;
      }
      // Dongle only when outputs are configured (else clients would pick a broken path).
      if (path.id == stream_path::k_headless_dongle) {
        const auto &cfg = config::video.linux_display;
        if (cfg.streaming_output.empty() || cfg.primary_output.empty() ||
            cfg.streaming_output == cfg.primary_output) {
          continue;
        }
      }
      modes.emplace_back(path.id);
    }
    return modes;
  }

}  // namespace stream_display_policy
