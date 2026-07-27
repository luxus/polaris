/**
 * @file src/platform/linux/stream_display_policy.cpp
 * @brief Linux stream display policy resolver.
 */

#include "stream_display_policy.h"

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

    resolved_t make_desktop() {
      resolved_t result;
      result.mode = mode_e::DESKTOP;
      result.selection = std::string {k_desktop_display};
      result.label = label_for_selection(k_desktop_display);
      result.reason = reason_for_selection(k_desktop_display);
      result.private_runtime = private_runtime_e::NONE;
      result.available = true;
      return result;
    }

    resolved_t make_host_virtual_display(bool virtual_display_available) {
      resolved_t result;
      result.mode = mode_e::HOST_VIRTUAL_DISPLAY;
      result.selection = std::string {k_host_virtual_display};
      result.label = label_for_selection(k_host_virtual_display);
      result.reason = reason_for_selection(k_host_virtual_display, virtual_display_available);
      result.private_runtime = private_runtime_e::NONE;
      result.available = true;
      result.requested_headless = true;
      result.effective_headless = false;
      result.use_host_virtual_display = true;
      return result;
    }

    private_runtime_e configured_private_runtime() {
      return parse_private_runtime(config::video.linux_display.private_runtime);
    }

    /**
     * @brief Gamescope private runtime is not implemented in this build.
     */
    bool private_runtime_available(private_runtime_e runtime) {
      switch (runtime) {
        case private_runtime_e::NONE:
        case private_runtime_e::LABWC:
          return true;
        case private_runtime_e::GAMESCOPE:
          return false;
      }
      return false;
    }

    void set_private_runtime_flags(resolved_t &result, private_runtime_e runtime) {
      result.private_runtime = runtime;
      result.use_private_runtime = runtime != private_runtime_e::NONE;
      // Cage-era call sites only understand labwc private runtime wrap semantics.
      result.use_cage_runtime = runtime == private_runtime_e::LABWC;
    }

  }  // namespace

  std::optional<mode_e> parse_selection(std::string_view selection) {
    const auto key = to_lower_copy(selection);
    if (key == k_headless_stream) {
      return mode_e::HEADLESS;
    }
    if (key == k_windowed_stream) {
      return mode_e::GPU_NATIVE_STREAM;
    }
    if (key == k_host_virtual_display) {
      return mode_e::HOST_VIRTUAL_DISPLAY;
    }
    if (key == k_desktop_display) {
      return mode_e::DESKTOP;
    }
    if (key == k_gamescope_stream) {
      return mode_e::GAMESCOPE_STREAM;
    }
    return std::nullopt;
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
    const auto key = to_lower_copy(selection);
    if (key == k_headless_stream) {
      return "Private Stream";
    }
    if (key == k_windowed_stream) {
      return "Private Stream (GPU-native)";
    }
    if (key == k_host_virtual_display) {
      return "Host Virtual Display";
    }
    if (key == k_gamescope_stream) {
      return "Gamescope Stream";
    }
    if (key == k_desktop_display) {
      return "Mirror Desktop";
    }
    return {};
  }

  std::string reason_for_selection(std::string_view selection, bool virtual_display_available) {
    const auto key = to_lower_copy(selection);
    if (key == k_headless_stream) {
      return "Polaris streams from a private headless compositor; GPU-native appears in session health when capture stays on DMA-BUF/GPU.";
    }
    if (key == k_windowed_stream) {
      return "Polaris can force a windowed private compositor when hidden Private Stream capture cannot stay GPU-native.";
    }
    if (key == k_host_virtual_display) {
      return virtual_display_available ?
               "Polaris will create or use a host virtual display for this stream." :
               "Polaris requested a host virtual display, but no backend is currently available.";
    }
    if (key == k_gamescope_stream) {
      return "Polaris will stream from a headless Gamescope session when the Gamescope runtime is available on this host.";
    }
    return "Polaris will mirror the current desktop session.";
  }

  bool selection_available(std::string_view selection) {
    const auto key = to_lower_copy(selection);
    if (key == k_gamescope_stream) {
      return false;
    }
    if (key == k_headless_stream || key == k_windowed_stream ||
        key == k_host_virtual_display || key == k_desktop_display) {
      return true;
    }
    return false;
  }

  std::string selection_unavailable_reason(std::string_view selection) {
    if (to_lower_copy(selection) == k_gamescope_stream) {
      return "Gamescope Stream is not available in this build; Private Stream uses labwc. A future release will own the Gamescope runtime.";
    }
    if (!selection_available(selection)) {
      return "Unknown stream display mode.";
    }
    return {};
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
    if (key == k_headless_stream) {
      booleans.headless_mode = true;
      booleans.use_cage_compositor = true;
      booleans.prefer_gpu_native_capture = false;
    }
    else if (key == k_windowed_stream) {
      booleans.headless_mode = true;
      booleans.use_cage_compositor = true;
      booleans.prefer_gpu_native_capture = true;
    }
    else if (key == k_host_virtual_display) {
      booleans.headless_mode = true;
      booleans.use_cage_compositor = false;
      booleans.prefer_gpu_native_capture = false;
    }
    else if (key == k_gamescope_stream) {
      // Future: private gamescope runtime without labwc cage flags.
      booleans.headless_mode = true;
      booleans.use_cage_compositor = false;
      booleans.prefer_gpu_native_capture = false;
    }
    else {
      // desktop_display and unknown → mirror desktop
      booleans.headless_mode = false;
      booleans.use_cage_compositor = false;
      booleans.prefer_gpu_native_capture = false;
    }
    return booleans;
  }

  private_runtime_e parse_private_runtime(std::string_view value) {
    const auto key = to_lower_copy(value);
    if (key.empty() || key == k_runtime_labwc) {
      return private_runtime_e::LABWC;
    }
    if (key == k_runtime_gamescope) {
      return private_runtime_e::GAMESCOPE;
    }
    return private_runtime_e::LABWC;
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
    auto &linux_display = config::video.linux_display;

    // Prefer explicit stream_mode when set; otherwise derive from legacy booleans.
    std::string configured_selection = linux_display.stream_mode;
    if (configured_selection.empty()) {
      configured_selection = selection_from_legacy_booleans({
        linux_display.headless_mode,
        linux_display.use_cage_compositor,
        linux_display.prefer_gpu_native_capture,
      });
    }

    const auto parsed = parse_selection(configured_selection);
    if (!parsed) {
      return make_desktop();
    }

    if (*parsed == mode_e::GAMESCOPE_STREAM) {
      resolved_t result;
      result.mode = mode_e::GAMESCOPE_STREAM;
      result.selection = std::string {k_gamescope_stream};
      result.label = label_for_selection(k_gamescope_stream);
      result.reason = reason_for_selection(k_gamescope_stream);
      result.private_runtime = private_runtime_e::GAMESCOPE;
      result.available = false;
      result.unavailable_reason = selection_unavailable_reason(k_gamescope_stream);
      result.requested_headless = true;
      result.effective_headless = true;
      set_private_runtime_flags(result, private_runtime_e::GAMESCOPE);
      result.use_private_runtime = false;  // cannot start until backend exists
      result.use_cage_runtime = false;
      return result;
    }

    if (*parsed == mode_e::HOST_VIRTUAL_DISPLAY) {
      return make_host_virtual_display(input.virtual_display_available);
    }

    // Explicit desktop mode, or legacy !headless without cage.
    if (*parsed == mode_e::DESKTOP &&
        !(linux_display.use_cage_compositor && linux_display.stream_mode.empty())) {
      return make_desktop();
    }

    // Private labwc family: headless_stream, windowed_stream, or legacy cage without headless.
    const bool requested_headless =
      (*parsed == mode_e::HEADLESS || *parsed == mode_e::GPU_NATIVE_STREAM) ?
        true :
        linux_display.headless_mode;
    const bool use_private =
      (*parsed == mode_e::HEADLESS || *parsed == mode_e::GPU_NATIVE_STREAM ||
       *parsed == mode_e::WINDOWED_STREAM) ?
        true :
        linux_display.use_cage_compositor;

    // Legacy: stream_mode empty/desktop-derived but cage on without headless → windowed private.
    if (!requested_headless && use_private) {
      // Windowed private compositor on the current desktop.
      const bool gpu_native_test =
        linux_display.prefer_gpu_native_capture ||
        input.active_encoder_requires_gpu_native_capture ||
        input.runtime_gpu_native_override_active;

      resolved_t result;
      result.mode = gpu_native_test ? mode_e::GPU_NATIVE_STREAM : mode_e::WINDOWED_STREAM;
      result.selection = std::string {k_windowed_stream};
      result.label = gpu_native_test ? label_for_selection(k_windowed_stream) : "Private Stream (windowed)";
      result.reason = gpu_native_test ?
                        "Polaris is running the private compositor windowed so capture can stay GPU-native." :
                        "Polaris streams from a private compositor window on the current desktop.";
      result.available = true;
      result.requested_headless = false;
      result.effective_headless = false;
      result.prefer_gpu_native_capture = gpu_native_test;
      result.should_defer_encoder_probe = true;
      result.should_probe_against_runtime = true;
      set_private_runtime_flags(result, private_runtime_e::LABWC);
      return result;
    }

    if (!use_private) {
      return make_host_virtual_display(input.virtual_display_available);
    }

    const auto runtime = configured_private_runtime();
    if (runtime == private_runtime_e::GAMESCOPE && !private_runtime_available(runtime)) {
      // Config asked for gamescope private runtime under a private mode — fall back labeling
      // but mark unavailable for start; resolve still describes intent.
      resolved_t result;
      result.mode = mode_e::GAMESCOPE_STREAM;
      result.selection = std::string {k_gamescope_stream};
      result.label = label_for_selection(k_gamescope_stream);
      result.reason = selection_unavailable_reason(k_gamescope_stream);
      result.available = false;
      result.unavailable_reason = selection_unavailable_reason(k_gamescope_stream);
      result.requested_headless = true;
      set_private_runtime_flags(result, private_runtime_e::GAMESCOPE);
      result.use_private_runtime = false;
      result.use_cage_runtime = false;
      return result;
    }

    const bool gpu_native_test =
      linux_display.prefer_gpu_native_capture ||
      input.active_encoder_requires_gpu_native_capture ||
      input.runtime_gpu_native_override_active ||
      *parsed == mode_e::GPU_NATIVE_STREAM;

    resolved_t result;
    result.requested_headless = true;
    result.should_defer_encoder_probe = true;
    result.should_probe_against_runtime = true;
    result.prefer_gpu_native_capture = gpu_native_test;
    result.available = true;
    set_private_runtime_flags(result, private_runtime_e::LABWC);

    if (gpu_native_test) {
      result.mode = mode_e::GPU_NATIVE_STREAM;
      result.selection = std::string {k_windowed_stream};
      result.label = label_for_selection(k_windowed_stream);
      result.reason = input.runtime_gpu_native_override_active ?
                        "Polaris is running the private compositor windowed so capture can stay GPU-native." :
                        "Polaris can force a windowed private compositor when hidden Private Stream capture cannot stay GPU-native.";
      result.effective_headless = !input.runtime_gpu_native_override_active;
      return result;
    }

    result.mode = mode_e::HEADLESS;
    result.selection = std::string {k_headless_stream};
    result.label = label_for_selection(k_headless_stream);
    result.reason = reason_for_selection(k_headless_stream);
    result.effective_headless = true;
    return result;
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

    const auto configured = resolve(input);
    if (input.runtime_gpu_native_override_active) {
      resolved_t result = configured;
      result.mode = mode_e::GPU_NATIVE_STREAM;
      result.selection = std::string {k_windowed_stream};
      result.label = label_for_selection(k_windowed_stream);
      result.effective_headless = false;
      result.prefer_gpu_native_capture = true;
      return result;
    }
    if (session_uses_virtual_display) {
      return make_host_virtual_display(input.virtual_display_available);
    }
    if (configured.selection == k_windowed_stream && runtime_effective_headless) {
      return configured;
    }
    if (runtime_effective_headless) {
      resolved_t result = configured;
      result.mode = mode_e::HEADLESS;
      result.selection = std::string {k_headless_stream};
      result.label = label_for_selection(k_headless_stream);
      result.effective_headless = true;
      return result;
    }
    if (configured.use_private_runtime || configured.use_cage_runtime) {
      return configured;
    }
    return make_desktop();
  }

  bool apply_selection(std::string_view selection, std::string &error) {
    const auto key = to_lower_copy(selection);
    if (!parse_selection(key)) {
      error = "stream_display_mode must be headless_stream, desktop_display, host_virtual_display, windowed_stream, or gamescope_stream";
      return false;
    }
    if (!selection_available(key)) {
      error = selection_unavailable_reason(key);
      return false;
    }

    const auto booleans = legacy_booleans_for_selection(key);
    auto &linux_display = config::video.linux_display;
    linux_display.stream_mode = key;
    linux_display.headless_mode = booleans.headless_mode;
    linux_display.use_cage_compositor = booleans.use_cage_compositor;
    linux_display.prefer_gpu_native_capture = booleans.prefer_gpu_native_capture;

    // Private modes use labwc until gamescope is implemented.
    if (booleans.use_cage_compositor || key == k_headless_stream || key == k_windowed_stream) {
      linux_display.private_runtime = std::string {k_runtime_labwc};
    }
    else if (key == k_gamescope_stream) {
      linux_display.private_runtime = std::string {k_runtime_gamescope};
    }
    else {
      // Mirror / host vdisplay — no private runtime required.
      if (linux_display.private_runtime.empty()) {
        linux_display.private_runtime = std::string {k_runtime_labwc};
      }
    }

    return true;
  }

  void normalize_config_from_load() {
    auto &linux_display = config::video.linux_display;

    if (!linux_display.stream_mode.empty()) {
      if (const auto parsed = parse_selection(linux_display.stream_mode)) {
        // Explicit mode wins: sync legacy booleans for older code paths.
        // Do not apply unavailable gamescope into cage flags via apply_selection
        // (that rejects); still sync intended booleans for honesty.
        if (*parsed == mode_e::GAMESCOPE_STREAM) {
          const auto booleans = legacy_booleans_for_selection(k_gamescope_stream);
          linux_display.headless_mode = booleans.headless_mode;
          linux_display.use_cage_compositor = booleans.use_cage_compositor;
          linux_display.prefer_gpu_native_capture = booleans.prefer_gpu_native_capture;
          linux_display.private_runtime = std::string {k_runtime_gamescope};
          return;
        }
        const auto booleans = legacy_booleans_for_selection(linux_display.stream_mode);
        linux_display.headless_mode = booleans.headless_mode;
        linux_display.use_cage_compositor = booleans.use_cage_compositor;
        linux_display.prefer_gpu_native_capture = booleans.prefer_gpu_native_capture;
        if (booleans.use_cage_compositor) {
          if (linux_display.private_runtime.empty() ||
              parse_private_runtime(linux_display.private_runtime) == private_runtime_e::GAMESCOPE) {
            // Prefer labwc for private cage modes until gamescope works.
            linux_display.private_runtime = std::string {k_runtime_labwc};
          }
        }
        return;
      }
      // Unknown stream_mode: clear and fall back to booleans.
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
    std::vector<mode_option_t> options;
    for (const auto selection : {
           k_headless_stream,
           k_desktop_display,
           k_host_virtual_display,
           k_windowed_stream,
           k_gamescope_stream,
         }) {
      mode_option_t option;
      option.value = std::string {selection};
      option.label = label_for_selection(selection);
      option.reason = reason_for_selection(selection, virtual_display_available);
      option.available = selection_available(selection);
      option.unavailable_reason = selection_unavailable_reason(selection);
      options.push_back(std::move(option));
    }
    return options;
  }

  std::vector<std::string> allowed_launch_modes(bool virtual_display_available,
                                                bool include_unavailable) {
    std::vector<std::string> modes;
    modes.emplace_back(k_headless_stream);
    modes.emplace_back(k_desktop_display);
    modes.emplace_back(k_windowed_stream);
    if (virtual_display_available) {
      modes.emplace_back(k_host_virtual_display);
    }
    if (include_unavailable) {
      modes.emplace_back(k_gamescope_stream);
    }
    return modes;
  }

}  // namespace stream_display_policy
