/**
 * @file src/browser_stream.h
 * @brief Browser Stream API and session metadata helpers.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace browser_stream {
  constexpr std::uint16_t default_webtransport_port = 47992;

  struct session_token_t {
    std::string token;
    std::chrono::steady_clock::time_point expires_at;
  };

  bool build_enabled();
  nlohmann::json status_json();
  nlohmann::json create_session(
    std::string_view remote_address,
    std::string_view host,
    std::string_view app_uuid = {},
    std::string_view profile_id = "balanced"
  );
  nlohmann::json submit_input(std::string_view token, const nlohmann::json &events);

  /**
   * @brief Result of stop_session — token match and whether an owned app remains.
   *
   * SB-2: HTTP stop handlers respond before nested app terminate; they call
   * stop_session(..., terminate_owned_app=false) then terminate after write.
   */
  struct stop_session_result_t {
    bool stopped = false;  ///< session token was valid / consumed
    bool owns_app = false;  ///< caller should terminate nested app if not done here
  };

  /**
   * @brief Stop a Browser Stream session (helper + capture).
   * @param terminate_owned_app When true (default), call proc::terminate if the
   *   session owned the nested app. HTTP handlers pass false and terminate after
   *   the response so :47990 cannot wedge on gamescope kill.
   */
  stop_session_result_t stop_session(std::string_view token, bool terminate_owned_app = true);

  /**
   * @brief Synchronously stop Browser Stream media capture before nested teardown.
   *
   * SB-2 order: signal shutdown → release portal/PipeWire → bounded join (≤3s).
   * Never blocks confighttp past the join budget. Must run before gamescope/labwc
   * kill so PipeWire is not attached into a dying compositor.
   */
  void prepare_for_session_teardown();
  session_token_t issue_session_token(std::string_view remote_address, std::string_view app_uuid = {}, bool owns_app = false);
  bool consume_session_token(std::string_view token, std::string_view remote_address);
}  // namespace browser_stream
