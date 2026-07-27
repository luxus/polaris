/**
 * @file src/platform/linux/stream_display_policy.h
 * @brief Resolves Linux stream display mode and private runtime selection.
 *
 * Single source of truth for user-facing stream modes, labels, and whether a
 * private compositor runtime (labwc today; gamescope later) is required.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stream_display_policy {

  /**
   * @brief User-facing / internal stream mode identity.
   *
   * Stable selection strings for API/UI: headless_stream, windowed_stream,
   * host_virtual_display, desktop_display, gamescope_stream.
   */
  enum class mode_e {
    DESKTOP,
    HEADLESS,
    HOST_VIRTUAL_DISPLAY,
    WINDOWED_STREAM,
    GPU_NATIVE_STREAM,
    GAMESCOPE_STREAM,
  };

  /**
   * @brief Private nested compositor backend for Private Stream modes.
   */
  enum class private_runtime_e {
    NONE,
    LABWC,
    GAMESCOPE,
  };

  struct input_t {
    bool virtual_display_available = false;
    bool active_encoder_requires_gpu_native_capture = false;
    bool runtime_gpu_native_override_active = false;
  };

  struct resolved_t {
    mode_e mode = mode_e::DESKTOP;
    std::string selection;
    std::string label;
    std::string reason;
    private_runtime_e private_runtime = private_runtime_e::NONE;
    bool available = true;
    std::string unavailable_reason;
    bool requested_headless = false;
    bool effective_headless = false;
    /// True when a private nested compositor must host the session.
    bool use_private_runtime = false;
    /// Back-compat alias for labwc-era call sites (same as use_private_runtime for labwc).
    bool use_cage_runtime = false;
    bool use_host_virtual_display = false;
    bool prefer_gpu_native_capture = false;
    bool should_defer_encoder_probe = false;
    bool should_probe_against_runtime = false;
  };

  struct mode_option_t {
    std::string value;
    std::string label;
    std::string reason;
    bool available = true;
    std::string unavailable_reason;
  };

  struct legacy_booleans_t {
    bool headless_mode = false;
    bool use_cage_compositor = false;
    bool prefer_gpu_native_capture = false;
  };

  /** Stable selection id strings. */
  constexpr std::string_view k_headless_stream = "headless_stream";
  constexpr std::string_view k_windowed_stream = "windowed_stream";
  constexpr std::string_view k_host_virtual_display = "host_virtual_display";
  constexpr std::string_view k_desktop_display = "desktop_display";
  constexpr std::string_view k_gamescope_stream = "gamescope_stream";

  constexpr std::string_view k_runtime_labwc = "labwc";
  constexpr std::string_view k_runtime_gamescope = "gamescope";

  /**
   * @brief Parse a configured mode selection string.
   * @return nullopt when the string is empty or unknown.
   */
  std::optional<mode_e> parse_selection(std::string_view selection);

  /**
   * @brief Stable API selection id for a mode.
   */
  std::string_view selection_for_mode(mode_e mode);

  /**
   * @brief Human label for a selection id.
   */
  std::string label_for_selection(std::string_view selection);

  /**
   * @brief Reason copy for a selection id (may depend on virtual display availability).
   */
  std::string reason_for_selection(std::string_view selection, bool virtual_display_available = false);

  /**
   * @brief Whether a mode is available on this host build.
   *
   * gamescope_stream is registered but not implemented yet.
   */
  bool selection_available(std::string_view selection);

  std::string selection_unavailable_reason(std::string_view selection);

  /**
   * @brief Derive the configured selection from legacy booleans when
   *        linux_stream_mode is unset.
   */
  std::string selection_from_legacy_booleans(const legacy_booleans_t &booleans);

  /**
   * @brief Map a selection id to the legacy boolean triple used by older clients.
   */
  legacy_booleans_t legacy_booleans_for_selection(std::string_view selection);

  /**
   * @brief Map private_runtime config string to enum (default labwc).
   */
  private_runtime_e parse_private_runtime(std::string_view value);

  std::string_view private_runtime_id(private_runtime_e runtime);

  /**
   * @brief Resolve the configured (desired) mode from config + optional inputs.
   */
  resolved_t resolve(const input_t &input = {});

  /**
   * @brief Convenience: resolve using virtual_display::is_available() and flags.
   */
  resolved_t resolve_current(bool active_encoder_requires_gpu_native_capture = false,
                             bool runtime_gpu_native_override_active = false);

  /**
   * @brief Resolve the effective mode while a session is live.
   */
  resolved_t resolve_effective(const input_t &input,
                               bool streaming,
                               bool session_uses_virtual_display,
                               bool runtime_effective_headless);

  /**
   * @brief Apply a user/API selection into config (stream_mode + legacy bools + runtime).
   *
   * Rejects gamescope_stream until a gamescope backend exists.
   * @return false and sets error on failure.
   */
  bool apply_selection(std::string_view selection, std::string &error);

  /**
   * @brief Normalize config after load: if stream_mode set, sync booleans; else
   *        leave booleans authoritative and fill stream_mode in memory.
   */
  void normalize_config_from_load();

  /**
   * @brief Options list for client-settings / UI (includes unavailable gamescope).
   */
  std::vector<mode_option_t> mode_options(bool virtual_display_available = false);

  /**
   * @brief Allowed launch-mode selection ids for Nova (excludes unavailable modes).
   */
  std::vector<std::string> allowed_launch_modes(bool virtual_display_available,
                                                bool include_unavailable = false);

}  // namespace stream_display_policy
