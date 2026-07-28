/**
 * @file src/platform/linux/stream_runtime_labwc.cpp
 * @brief labwc stream_runtime adapter over cage_display_router.
 */

#include "stream_runtime.h"

#ifdef __linux__

  #include "cage_display_router.h"

  #include <memory>

namespace stream_runtime {

  namespace {

    class labwc_runtime_t: public stream_runtime_t {
    public:
      std::string_view backend_id() const override {
        return stream_display_policy::k_runtime_labwc;
      }

      bool start(const start_params_t &params) override {
        return cage_display_router::start(
          params.width,
          params.height,
          params.refresh_hz,
          params.game_cmd,
          params.force_windowed,
          params.allow_mangohud,
          params.session_instance_id
        );
      }

      void stop() override {
        cage_display_router::stop();
      }

      void reset_after_external_stop() override {
        cage_display_router::reset_after_external_stop();
      }

      bool is_running() const override {
        return cage_display_router::is_running();
      }

      bool is_healthy() const override {
        return cage_display_router::is_healthy();
      }

      pid_t pid() const override {
        return cage_display_router::get_pid();
      }

      std::string wrap_cmd(const std::string &cmd) const override {
        return cage_display_router::wrap_cmd(cmd);
      }

      std::string wayland_socket() const override {
        return cage_display_router::get_wayland_socket();
      }

      std::string x11_display() const override {
        return cage_display_router::get_x11_display();
      }

      platf::runtime_state_t runtime_state() const override {
        return cage_display_router::runtime_state();
      }
    };

    // Single process-wide labwc adapter (matches historical cage_display_router globals).
    std::shared_ptr<labwc_runtime_t> g_labwc_runtime = std::make_shared<labwc_runtime_t>();

  }  // namespace

  // Provided by stream_runtime_gamescope.cpp
  std::shared_ptr<stream_runtime_t> gamescope_runtime_instance();

  std::shared_ptr<stream_runtime_t> acquire(stream_display_policy::private_runtime_e runtime) {
    using stream_display_policy::private_runtime_e;
    switch (runtime) {
      case private_runtime_e::LABWC:
        return g_labwc_runtime;
      case private_runtime_e::GAMESCOPE:
        return gamescope_runtime_instance();
      case private_runtime_e::NONE:
      default:
        return nullptr;
    }
  }

  std::shared_ptr<stream_runtime_t> acquire_for_current_policy(
    bool active_encoder_requires_gpu_native_capture,
    bool runtime_gpu_native_override_active
  ) {
    const auto resolved = stream_display_policy::resolve_current(
      active_encoder_requires_gpu_native_capture,
      runtime_gpu_native_override_active
    );
    if (resolved.private_runtime == stream_display_policy::private_runtime_e::GAMESCOPE) {
      return acquire(stream_display_policy::private_runtime_e::GAMESCOPE);
    }
    if (!resolved.use_private_runtime && !resolved.use_cage_runtime) {
      return nullptr;
    }
    return acquire(resolved.private_runtime);
  }

  bool is_labwc_backend(const stream_runtime_t *runtime) {
    return runtime && runtime->backend_id() == stream_display_policy::k_runtime_labwc;
  }

}  // namespace stream_runtime

#endif  // __linux__
