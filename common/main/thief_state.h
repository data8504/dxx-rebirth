/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include "aistruct.h"

#ifdef DXX_BUILD_DESCENT
#if DXX_BUILD_DESCENT == 2
namespace dsx {

struct thief_state
{
	ai_mode mode;
	fix next_action_time;
};

static inline thief_state capture_thief_state(const ai_mode mode, const fix remaining)
{
	return {mode, std::max(remaining, fix{})};
}

static inline std::optional<thief_state> build_thief_state_from_untrusted(const uint8_t untrusted_mode, const fix remaining)
{
	const auto mode = ai_mode{untrusted_mode};
	switch (mode) {
		case ai_mode::AIM_THIEF_ATTACK:
		case ai_mode::AIM_THIEF_RETREAT:
		case ai_mode::AIM_THIEF_WAIT:
			break;
		default:
			return {};
	}
	if (remaining < 0)
		return {};
	return thief_state{mode, remaining};
}

}
#endif
#endif
