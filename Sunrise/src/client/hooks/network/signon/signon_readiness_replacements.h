#pragma once

namespace sunrise::client::hooks::network::signon {

/** @return The SignOn readiness-failure check replacement body. */
[[nodiscard]] void* readiness_entry_point() noexcept;

/** @return The SignOn readiness-ready check replacement body. */
[[nodiscard]] void* ready_entry_point() noexcept;

} // namespace sunrise::client::hooks::network::signon
