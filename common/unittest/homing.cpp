#include "homing.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth homing
#include <boost/test/unit_test.hpp>

namespace {

using namespace dcx;

static constexpr fix homing_turn_time{F1_0 / 30};
static constexpr fix d2_retention_dot{7 * F1_0 / 8 + F1_0 / 64 - F1_0 / 16 - homing_turn_time};

struct trajectory_result
{
	vms_vector position;
	vms_vector velocity;
	vms_matrix orientation;
	unsigned turns;
};

static trajectory_result run_open_space_robot_homer(const int stored_track_goal)
{
	static constexpr int player_object{17};
	static constexpr unsigned tracker_object{6};
	vms_vector position{};
	vms_vector velocity{.x = i2f(20), .y = 0, .z = 0};
	vms_matrix orientation{
		.rvec = {.x = 0, .y = -F1_0, .z = 0},
		.uvec = {.x = 0, .y = 0, .z = F1_0},
		.fvec = {.x = F1_0, .y = 0, .z = 0},
	};
	unsigned turns{};
	for (unsigned tick = 1; tick <= 60; ++tick)
	{
		const vms_vector target_position{
			.x = i2f(100 + static_cast<int>(tick)),
			.y = i2f(static_cast<int>(tick)) / 2,
			.z = 0,
		};
		const auto target_vector{vm_vec_build_sub(target_position, position)};
		auto target_direction{vm_vec_normalized_quick(target_vector)};
		const auto target_dot{vm_vec_build_dot(target_direction, orientation.fvec)};
		const bool retained{stored_track_goal == player_object && target_dot >= d2_retention_dot && ((tracker_object ^ tick) % 8)};
		const bool acquired{!retained && !((tracker_object ^ tick) % 4) && target_dot > 7 * F1_0 / 8};
		if (retained || acquired)
		{
			const auto turn{homing_turn_velocity(velocity, target_vector, i2f(40), homing_turn_time, false)};
			velocity = turn.velocity;
			orientation = homing_turn_orientation(orientation, turn.normalized_velocity, homing_turn_time, 16);
			++turns;
		}
		vm_vec_scale_add2(position, velocity, homing_turn_time);
	}
	return {position, velocity, orientation, turns};
}

BOOST_AUTO_TEST_CASE(single_player_robot_homer_starts_with_player_target)
{
	BOOST_CHECK_EQUAL(initial_homing_track_goal(false, true, 17, -1), 17);
	BOOST_CHECK_EQUAL(initial_homing_track_goal(false, false, 17, -1), -1);
	BOOST_CHECK_EQUAL(initial_homing_track_goal(true, true, 17, -1), -1);
}

BOOST_AUTO_TEST_CASE(single_player_robot_homer_matches_persistent_player_oracle)
{
	const auto repaired{run_open_space_robot_homer(initial_homing_track_goal(false, true, 17, -1))};
	const auto original_oracle{run_open_space_robot_homer(17)};
	const auto no_target_characterization{run_open_space_robot_homer(-1)};
	BOOST_CHECK_EQUAL(original_oracle.position.x, 4348568);
	BOOST_CHECK_EQUAL(original_oracle.position.y, 568252);
	BOOST_CHECK_EQUAL(original_oracle.velocity.x, 2376199);
	BOOST_CHECK_EQUAL(original_oracle.velocity.y, 533998);
	BOOST_CHECK_EQUAL(original_oracle.orientation.fvec.x, 64022);
	BOOST_CHECK_EQUAL(original_oracle.orientation.fvec.y, 14002);
	BOOST_CHECK_EQUAL(repaired.turns, 60);
	BOOST_CHECK_EQUAL(repaired.position.x, original_oracle.position.x);
	BOOST_CHECK_EQUAL(repaired.position.y, original_oracle.position.y);
	BOOST_CHECK_EQUAL(repaired.velocity.x, original_oracle.velocity.x);
	BOOST_CHECK_EQUAL(repaired.velocity.y, original_oracle.velocity.y);
	BOOST_CHECK_EQUAL(repaired.orientation.fvec.x, original_oracle.orientation.fvec.x);
	BOOST_CHECK_EQUAL(repaired.orientation.fvec.y, original_oracle.orientation.fvec.y);
	BOOST_CHECK_EQUAL(no_target_characterization.turns, 15);
	BOOST_CHECK_EQUAL(no_target_characterization.position.x, 3165625);
	BOOST_CHECK_EQUAL(no_target_characterization.position.y, 351497);
	BOOST_CHECK_GT(static_cast<fix>(vm_vec_dist(repaired.position, no_target_characterization.position)), i2f(10));
}

BOOST_AUTO_TEST_CASE(d2_polygon_turn_matches_original_30hz_kernel)
{
	const auto result{homing_turn_velocity(
		{.x = i2f(20), .y = 0, .z = 0},
		{.x = i2f(30), .y = i2f(40), .z = 0},
		i2f(40), homing_turn_time, false)};
	BOOST_CHECK_EQUAL(result.velocity.x, 1143332);
	BOOST_CHECK_EQUAL(result.velocity.y, 562872);
	BOOST_CHECK_EQUAL(result.velocity.z, 0);
	BOOST_CHECK_EQUAL(result.normalized_velocity.x, 55323);
	BOOST_CHECK_EQUAL(result.normalized_velocity.y, 27236);
	BOOST_CHECK_EQUAL(result.normalized_velocity.z, 0);
	BOOST_CHECK_EQUAL(result.velocity_target_dot, 38362);
}

BOOST_AUTO_TEST_CASE(d2_polygon_orientation_matches_original_30hz_kernel)
{
	const vms_matrix orientation{
		.rvec = {.x = F1_0, .y = 0, .z = 0},
		.uvec = {.x = 0, .y = F1_0, .z = 0},
		.fvec = {.x = F1_0, .y = 0, .z = 0},
	};
	const auto result{homing_turn_orientation(orientation, {.x = 55323, .y = 27236, .z = 0}, homing_turn_time, 16)};
	BOOST_CHECK_EQUAL(result.fvec.x, 64784);
	BOOST_CHECK_EQUAL(result.fvec.y, 9898);
	BOOST_CHECK_EQUAL(result.fvec.z, 0);
}

BOOST_AUTO_TEST_CASE(d1_polygon_orientation_matches_original_30hz_kernel)
{
	const vms_matrix orientation{
		.rvec = {.x = F1_0, .y = 0, .z = 0},
		.uvec = {.x = 0, .y = F1_0, .z = 0},
		.fvec = {.x = F1_0, .y = 0, .z = 0},
	};
	const auto result{homing_turn_orientation(orientation, {.x = 1143332, .y = 562872, .z = 0}, homing_turn_time, 8)};
	BOOST_CHECK_EQUAL(result.fvec.x, 60739);
	BOOST_CHECK_EQUAL(result.fvec.y, 24610);
	BOOST_CHECK_EQUAL(result.fvec.z, 0);
}

}
