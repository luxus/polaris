/**
 * @file src/platform/linux/stream_runtime_gamescope.cpp
 * @brief Owned / attach Gamescope private stream runtime.
 *
 * Prefer attaching to an existing gamescope-0 (idle unit). If absent, spawn a
 * headless gamescope that owns gamescope-0 so portal capture stays stable.
 */

#include "stream_runtime.h"

#ifdef __linux__

  #include "src/logging.h"

  #include <atomic>
  #include <sstream>
  #include <chrono>
  #include <cstdlib>
  #include <cstring>
  #include <filesystem>
  #include <fstream>
  #include <mutex>
  #include <signal.h>
  #include <string>
  #include <string_view>
  #include <cstdio>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <thread>
  #include <unistd.h>
  #include <vector>

using namespace std::literals;

namespace stream_runtime {
  namespace {

    namespace fs = std::filesystem;

    std::string xdg_runtime_dir() {
      if (const char *xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg) {
        return xdg;
      }
      return "/run/user/" + std::to_string(getuid());
    }

    bool socket_exists(const std::string &name) {
      return fs::exists(xdg_runtime_dir() + "/" + name);
    }

    bool binary_on_path(const char *name) {
      const char *path_env = std::getenv("PATH");
      if (!path_env || !name) {
        return false;
      }
      std::string path {path_env};
      std::size_t start = 0;
      while (start <= path.size()) {
        const auto end = path.find(':', start);
        const auto dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty()) {
          const std::string candidate = dir + "/" + name;
          if (access(candidate.c_str(), X_OK) == 0) {
            return true;
          }
        }
        if (end == std::string::npos) {
          break;
        }
        start = end + 1;
      }
      return false;
    }

    class gamescope_runtime_t: public stream_runtime_t {
    public:
      std::string_view backend_id() const override {
        return stream_display_policy::k_runtime_gamescope;
      }

      bool start(const start_params_t &params) override {
        std::lock_guard lock(mu_);
        if (is_running_unlocked()) {
          refresh_runtime_state(params);
          return true;
        }

        // 1) Prefer attach to idle gamescope-0 (portal + polaris-hdr.env contract).
        if (try_attach_gamescope0(params)) {
          return true;
        }

        // 2) Ask the host idle unit to start (if present) before we spawn our own.
        if (try_start_idle_unit_and_attach(params)) {
          return true;
        }

        if (!binary_on_path("gamescope")) {
          BOOST_LOG(error) << "gamescope_runtime: gamescope not found on PATH"sv;
          return false;
        }

        // 3) Spawn owned headless gamescope — only if gamescope-0 is still free.
        // Never attach/spawn onto gamescope-1 (portal is hard-wired to gamescope-0).
        if (socket_exists("gamescope-0")) {
          return try_attach_gamescope0(params);
        }
        if (socket_exists("gamescope-1") && !socket_exists("gamescope-0")) {
          BOOST_LOG(warning) << "gamescope_runtime: stale gamescope-1 present without gamescope-0; cleaning and spawning owned gamescope-0"sv;
          cleanup_stale_sockets_aggressive();
        }

        const auto width = std::max(params.width, 640);
        const auto height = std::max(params.height, 480);
        const auto refresh = std::max(params.refresh_hz, 30);

        std::vector<std::string> args {
          "gamescope",
          "--backend",
          "headless",
          "--expose-wayland",
          "--steam",
          "--xwayland-count",
          "2",
          "-W",
          std::to_string(width),
          "-H",
          std::to_string(height),
          "-r",
          std::to_string(refresh),
          "-w",
          std::to_string(width),
          "-h",
          std::to_string(height),
          "--",
        };
        if (!params.game_cmd.empty()) {
          args.push_back("bash");
          args.push_back("-lc");
          args.push_back(params.game_cmd);
        }
        else {
          args.push_back("sleep");
          args.push_back("infinity");
        }

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (auto &a : args) {
          argv.push_back(a.data());
        }
        argv.push_back(nullptr);

        const pid_t child = fork();
        if (child < 0) {
          BOOST_LOG(error) << "gamescope_runtime: fork failed: "sv << std::strerror(errno);
          return false;
        }
        if (child == 0) {
          setsid();
          unsetenv("WAYLAND_DISPLAY");
          unsetenv("ENABLE_GAMESCOPE_WSI");
          unsetenv("ENABLE_HDR_WSI");
          execvp("gamescope", argv.data());
          _exit(127);
        }

        pid_ = child;
        owned_ = true;
        socket_name_ = "gamescope-0";

        for (int i = 0; i < 50; ++i) {
          if (socket_exists("gamescope-0")) {
            socket_name_ = "gamescope-0";
            break;
          }
          // If we got gamescope-1, fail — portal cannot use it reliably.
          if (socket_exists("gamescope-1") && !socket_exists("gamescope-0")) {
            BOOST_LOG(error) << "gamescope_runtime: owned spawn bound gamescope-1; aborting (portal requires gamescope-0)"sv;
            stop_unlocked();
            return false;
          }
          int status = 0;
          if (waitpid(pid_, &status, WNOHANG) == pid_) {
            BOOST_LOG(error) << "gamescope_runtime: gamescope exited before socket appeared"sv;
            pid_ = 0;
            owned_ = false;
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!socket_exists("gamescope-0")) {
          BOOST_LOG(error) << "gamescope_runtime: gamescope-0 never appeared"sv;
          stop_unlocked();
          return false;
        }

        x11_display_ = detect_x11_display();
        refresh_runtime_state(params);
        write_env_file();
        BOOST_LOG(info) << "gamescope_runtime: started owned gamescope pid="sv << pid_
                        << " socket=gamescope-0 "sv << width << "x"sv << height
                        << "@"sv << refresh;
        return true;
      }

      void stop() override {
        std::lock_guard lock(mu_);
        stop_unlocked();
      }

      void reset_after_external_stop() override {
        std::lock_guard lock(mu_);
        if (pid_ > 0) {
          // Non-blocking reap only.
          int status = 0;
          waitpid(pid_, &status, WNOHANG);
        }
        pid_ = 0;
        owned_ = false;
        socket_name_.clear();
        x11_display_.clear();
        state_ = {};
        state_.backend_name = "gamescope";
      }

      bool is_running() const override {
        std::lock_guard lock(mu_);
        return is_running_unlocked();
      }

      bool is_healthy() const override {
        std::lock_guard lock(mu_);
        if (!is_running_unlocked()) {
          return false;
        }
        return !socket_name_.empty() && socket_exists(socket_name_);
      }

      pid_t pid() const override {
        std::lock_guard lock(mu_);
        return pid_;
      }

      std::string wrap_cmd(const std::string &cmd) const override {
        std::lock_guard lock(mu_);
        if (socket_name_.empty() || !is_running_unlocked()) {
          return cmd;
        }
        // Attach path policy (SB-5): force X11 on gamescope XWayland so Steam/
        // native titles do not paint on host Wayland (black portal stream).
        // Nested WSI is only for the optional Big Picture polaris-hdr-session entry.
        // Still inject DXVK/Proton HDR env when polaris-hdr-force=1 so games can
        // present HDR10 into --hdr-enabled gamescope (stream path was already PQ).
        const auto display = x11_display_.empty() ? std::string {":1"} : x11_display_;
        bool want_game_hdr = false;
        {
          std::ifstream f(xdg_runtime_dir() + "/polaris-hdr-force");
          std::string line;
          if (f && std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
              line.pop_back();
            }
            want_game_hdr = (line == "1" || line == "true");
          }
        }
        std::ostringstream out;
        out << "env -u WAYLAND_DISPLAY -u CLUTTER_BACKEND "
            << "-u ELECTRON_OZONE_PLATFORM_HINT -u MOZ_ENABLE_WAYLAND "
            << "-u ENABLE_GAMESCOPE_WSI -u ENABLE_HDR_WSI "
            // Native Wayland Proton fights XWayland attach; drop if set in Steam launch opts.
            << "-u PROTON_ENABLE_WAYLAND "
            << "DISPLAY=" << shell_quote(display) << " "
            << "GAMESCOPE_WAYLAND_DISPLAY=" << shell_quote(socket_name_) << " "
            << "GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 XDG_SESSION_TYPE=x11 "
            << "QT_QPA_PLATFORM=xcb STEAM_MULTIPLE_XWAYLANDS=1 ";
        if (want_game_hdr) {
          out << "DXVK_HDR=1 PROTON_ENABLE_HDR=1 STEAM_GAMESCOPE_HDR_SUPPORTED=1 ";
        }
        out << "bash -lc " << shell_quote(cmd);
        return out.str();
      }

      std::string wayland_socket() const override {
        std::lock_guard lock(mu_);
        return socket_name_;
      }

      std::string x11_display() const override {
        std::lock_guard lock(mu_);
        return x11_display_;
      }

      platf::runtime_state_t runtime_state() const override {
        std::lock_guard lock(mu_);
        return state_;
      }

    private:
      mutable std::mutex mu_;
      pid_t pid_ = 0;
      bool owned_ = false;
      std::string socket_name_;
      std::string x11_display_;
      platf::runtime_state_t state_ {
        .requested_headless = true,
        .effective_headless = true,
        .gpu_native_override_active = false,
        .backend_name = "gamescope",
        .path_id = "gamescope_stream",
      };

      bool is_running_unlocked() const {
        if (owned_ && pid_ > 0) {
          if (kill(pid_, 0) != 0) {
            return false;
          }
          return socket_exists(socket_name_.empty() ? "gamescope-0" : socket_name_);
        }
        // Attach mode: socket presence is enough.
        return socket_exists(socket_name_.empty() ? "gamescope-0" : socket_name_);
      }

      void stop_unlocked() {
        if (owned_ && pid_ > 0) {
          // Signal process group (setsid child).
          kill(-pid_, SIGTERM);
          for (int i = 0; i < 30; ++i) {
            int status = 0;
            const auto r = waitpid(pid_, &status, WNOHANG);
            if (r == pid_ || (r < 0 && errno == ECHILD)) {
              break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
          if (kill(pid_, 0) == 0) {
            kill(-pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
          }
          BOOST_LOG(info) << "gamescope_runtime: stopped owned gamescope pid="sv << pid_;
        }
        else if (!owned_ && !socket_name_.empty()) {
          BOOST_LOG(info) << "gamescope_runtime: detach from "sv << socket_name_
                          << " (left running for idle unit)"sv;
        }
        pid_ = 0;
        owned_ = false;
        socket_name_.clear();
        x11_display_.clear();
        state_.backend_name = "gamescope";
      }

      void refresh_runtime_state(const start_params_t &params) {
        state_.requested_headless = true;
        state_.effective_headless = true;
        state_.gpu_native_override_active = false;
        state_.backend_name = "gamescope";
        state_.path_id = "gamescope_stream";
        (void) params;
      }

      static std::string shell_quote(const std::string &value) {
        std::string out = "'";
        for (char c : value) {
          if (c == '\'') {
            out += "'\\''";
          }
          else {
            out += c;
          }
        }
        out += "'";
        return out;
      }

      bool try_attach_gamescope0(const start_params_t &params) {
        if (!socket_exists("gamescope-0")) {
          return false;
        }
        owned_ = false;
        socket_name_ = "gamescope-0";
        x11_display_ = detect_x11_display();
        pid_ = 0;
        refresh_runtime_state(params);
        write_env_file();
        BOOST_LOG(info) << "gamescope_runtime: attached to existing gamescope-0 (idle/unit preferred)"sv;
        return true;
      }

      // polaris-hdr-idle reads $XDG_RUNTIME_DIR/polaris-hdr-force at start for --hdr-enabled.
      // process.cpp writes the file but does not restart the unit — so an idle started in
      // SDR stays SDR while portal claims HDR (BGRx + PQ washout). Restart when needed.
      static bool idle_hdr_flags_match_force() {
        const char *rt = std::getenv("XDG_RUNTIME_DIR");
        bool want_hdr = false;
        if (rt && *rt) {
          std::ifstream f(std::string(rt) + "/polaris-hdr-force");
          std::string line;
          if (f && std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
              line.pop_back();
            }
            want_hdr = (line == "1" || line == "true");
          }
        }
        // Inspect running headless gamescope cmdline for --hdr-enabled.
        FILE *pipe = popen(
          "pgrep -a -u \"$(id -u)\" -f 'gamescope .*--backend headless' 2>/dev/null | head -1",
          "r");
        if (!pipe) {
          return !want_hdr;
        }
        char buf[1024] {};
        const bool got = fgets(buf, sizeof(buf), pipe) != nullptr;
        pclose(pipe);
        if (!got) {
          return !want_hdr;
        }
        const bool running_hdr = std::string_view(buf).find("--hdr-enabled") != std::string_view::npos;
        return running_hdr == want_hdr;
      }

      static bool restart_or_start_idle_unit() {
        const int rc = std::system(
          "systemctl --user restart polaris-hdr-idle.service 2>/dev/null || "
          "systemctl --user start polaris-hdr-idle.service 2>/dev/null");
        return rc == 0;
      }

      bool try_start_idle_unit_and_attach(const start_params_t &params) {
        // Best-effort: host may ship polaris-hdr-idle.service that owns gamescope-0.
        const bool active =
          std::system("systemctl --user is-active --quiet polaris-hdr-idle.service 2>/dev/null") == 0;
        if (active && idle_hdr_flags_match_force()) {
          for (int i = 0; i < 30 && !socket_exists("gamescope-0"); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
          return try_attach_gamescope0(params);
        }
        if (active && !idle_hdr_flags_match_force()) {
          BOOST_LOG(info) << "gamescope_runtime: restarting polaris-hdr-idle to match polaris-hdr-force"sv;
        }
        if (restart_or_start_idle_unit()) {
          for (int i = 0; i < 80 && !socket_exists("gamescope-0"); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
          if (try_attach_gamescope0(params)) {
            BOOST_LOG(info) << "gamescope_runtime: polaris-hdr-idle ready (HDR flags synced); attached to gamescope-0"sv;
            return true;
          }
        }
        return false;
      }

      static void cleanup_stale_sockets_aggressive() {
        // Only used when we are about to spawn and gamescope-0 is free but -1 is leftover.
        std::system("pkill -u \"$(id -u)\" -f 'gamescope .*--backend headless' 2>/dev/null || true");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const auto rt = xdg_runtime_dir();
        std::error_code ec;
        for (const char *name : {
               "gamescope-0", "gamescope-0.lock", "gamescope-0-ei", "gamescope-0-ei.lock",
               "gamescope-1", "gamescope-1.lock", "gamescope-1-ei", "gamescope-1-ei.lock"
             }) {
          fs::remove(rt + "/" + name, ec);
        }
      }

      static std::string detect_x11_display() {
        // Headless gamescope typically exposes :1 / :2 for its XWayland servers.
        for (const char *disp : {":1", ":2", ":0"}) {
          const char *home = std::getenv("HOME");
          if (!home) {
            continue;
          }
          // Presence of X11 socket is a soft signal.
          if (fs::exists(std::string("/tmp/.X11-unix/X") + (disp + 1))) {
            return disp;
          }
        }
        return ":1";
      }

      void write_env_file() const {
        const auto path = xdg_runtime_dir() + "/polaris-hdr.env";
        std::ofstream out(path, std::ios::trunc);
        if (!out) {
          return;
        }
        out << "WAYLAND_DISPLAY=" << (socket_name_.empty() ? "gamescope-0" : socket_name_) << "\n";
        out << "GAMESCOPE_WAYLAND_DISPLAY=" << (socket_name_.empty() ? "gamescope-0" : socket_name_) << "\n";
        if (!x11_display_.empty()) {
          out << "DISPLAY=" << x11_display_ << "\n";
        }
      }
    };

    std::shared_ptr<gamescope_runtime_t> g_gamescope_runtime = std::make_shared<gamescope_runtime_t>();

  }  // namespace

  // Defined in stream_runtime_labwc.cpp — extend acquire there via weak link?
  // Provide the gamescope instance for labwc TU to pick up.
  std::shared_ptr<stream_runtime_t> gamescope_runtime_instance() {
    return g_gamescope_runtime;
  }

}  // namespace stream_runtime

#endif
