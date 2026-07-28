/**
 * @file src/platform/linux/stream_runtime.h
 * @brief Pluggable private stream compositor runtime (labwc + gamescope).
 *
 * process.cpp and browser_stream must start/stop/wrap sessions through this
 * interface instead of calling cage_display_router symbols directly.
 * Labwc-only capture probes may still use cage_display_router free functions
 * until those probes move behind this facade.
 */
#pragma once

#ifdef __linux__

  #include "src/platform/common.h"
  #include "stream_display_policy.h"

  #include <memory>
  #include <string>
  #include <string_view>
  #include <unistd.h>

namespace stream_runtime {

  struct start_params_t {
    int width = 1920;
    int height = 1080;
    int refresh_hz = 60;
    std::string game_cmd;
    bool force_windowed = false;
    bool allow_mangohud = true;
    std::string session_instance_id;
  };

  /**
   * @brief Lifecycle control for a private nested compositor used for streaming.
   */
  class stream_runtime_t {
  public:
    virtual ~stream_runtime_t() = default;

    virtual std::string_view backend_id() const = 0;

    virtual bool start(const start_params_t &params) = 0;
    virtual void stop() = 0;
    virtual void reset_after_external_stop() = 0;

    virtual bool is_running() const = 0;
    virtual bool is_healthy() const = 0;
    virtual pid_t pid() const = 0;

    virtual std::string wrap_cmd(const std::string &cmd) const = 0;
    virtual std::string wayland_socket() const = 0;
    virtual std::string x11_display() const = 0;
    virtual platf::runtime_state_t runtime_state() const = 0;
  };

  /**
   * @brief Acquire a private runtime implementation for the given backend.
   *
   * Returns nullptr when no private runtime is needed (mirror / host vdisplay)
   * or when the backend is not available yet (gamescope).
   */
  std::shared_ptr<stream_runtime_t> acquire(stream_display_policy::private_runtime_e runtime);

  /**
   * @brief Acquire the private runtime implied by the current stream policy.
   */
  std::shared_ptr<stream_runtime_t> acquire_for_current_policy(
    bool active_encoder_requires_gpu_native_capture = false,
    bool runtime_gpu_native_override_active = false
  );

  /**
   * @brief Labwc-only capture probe helpers remain free functions so other
   *        backends are not forced to implement labwc-specific paths.
   *
   * Prefer including cage_display_router.h at call sites that need probes, or
   * use these thin wrappers when only the active labwc runtime is in play.
   */
  bool is_labwc_backend(const stream_runtime_t *runtime);

}  // namespace stream_runtime

#endif  // __linux__
