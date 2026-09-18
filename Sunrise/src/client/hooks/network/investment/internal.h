#pragma once

#include "../../../patterns/image_scan.h"
#include "investment_patches.h"

namespace sunrise::client::hooks::network::investment {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/** Arms one derived-state rebuild, used up by the next freshness verdict. */
void arm_derived_rebuild() noexcept;

/**
 * Attaches the family-five commit rearm.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_family5_rearm() noexcept;

/** @return True when the family-five commit rearm is absent. */
[[nodiscard]] bool uninstall_family5_rearm() noexcept;

/** @return True while the family-five commit rearm is attached. */
[[nodiscard]] bool family5_rearm_is_installed() noexcept;

} // namespace sunrise::client::hooks::network::investment
