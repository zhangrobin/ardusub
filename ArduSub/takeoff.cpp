// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-

#include "Sub.h"

// This file contains the high-level takeoff logic for Loiter, PosHold, AltHold, Sport modes.
//   The take-off can be initiated from a GCS NAV_TAKEOFF command which includes a takeoff altitude
//   A safe takeoff speed is calculated and used to calculate a time_ms
//   the pos_control target is then slowly increased until time_ms expires

// return true if this flight mode supports user takeoff
//  must_nagivate is true if mode must also control horizontal position
bool Sub::current_mode_has_user_takeoff(bool must_navigate)
{
	return false; // not supported  in Sub
}

// initiate user takeoff - called when MAVLink TAKEOFF command is received
bool Sub::do_user_takeoff(float takeoff_alt_cm, bool must_navigate)
{
    return false; // not supported in Sub
}

// start takeoff to specified altitude above home in centimeters
void Sub::takeoff_timer_start(float alt_cm)
{
    // calculate climb rate
    float speed = MIN(wp_nav.get_speed_up(), MAX(g.pilot_velocity_z_max*2.0f/3.0f, g.pilot_velocity_z_max-50.0f));

    // sanity check speed and target
    if (takeoff_state.running || speed <= 0.0f || alt_cm <= 0.0f) {
        return;
    }

    // initialise takeoff state
    takeoff_state.running = true;
    takeoff_state.max_speed = speed;
    takeoff_state.start_ms = millis();
    takeoff_state.alt_delta = alt_cm;
}

// stop takeoff
void Sub::takeoff_stop()
{
    takeoff_state.running = false;
    takeoff_state.start_ms = 0;
}

// returns pilot and takeoff climb rates
//  pilot_climb_rate is both an input and an output
//  takeoff_climb_rate is only an output
//  has side-effect of turning takeoff off when timeout as expired
void Sub::takeoff_get_climb_rates(float& pilot_climb_rate, float& takeoff_climb_rate)
{
    // return pilot_climb_rate if take-off inactive
    if (!takeoff_state.running) {
        takeoff_climb_rate = 0.0f;
        return;
    }

    // acceleration of 50cm/s/s
    static const float takeoff_accel = 50.0f;
    float takeoff_minspeed = MIN(50.0f,takeoff_state.max_speed);
    float time_elapsed = (millis()-takeoff_state.start_ms)*1.0e-3f;
    float speed = MIN(time_elapsed*takeoff_accel+takeoff_minspeed, takeoff_state.max_speed);

    float time_to_max_speed = (takeoff_state.max_speed-takeoff_minspeed)/takeoff_accel;
    float height_gained;
    if (time_elapsed <= time_to_max_speed) {
        height_gained = 0.5f*takeoff_accel*sq(time_elapsed) + takeoff_minspeed*time_elapsed;
    } else {
        height_gained = 0.5f*takeoff_accel*sq(time_to_max_speed) + takeoff_minspeed*time_to_max_speed +
                        (time_elapsed-time_to_max_speed)*takeoff_state.max_speed;
    }

    // check if the takeoff is over
    if (height_gained >= takeoff_state.alt_delta) {
        takeoff_stop();
    }

    // if takeoff climb rate is zero return
    if (speed <= 0.0f) {
        takeoff_climb_rate = 0.0f;
        return;
    }

    // default take-off climb rate to maximum speed
    takeoff_climb_rate = speed;

    // if pilot's commands descent
    if (pilot_climb_rate < 0.0f) {
        // if overall climb rate is still positive, move to take-off climb rate
        if (takeoff_climb_rate + pilot_climb_rate > 0.0f) {
            takeoff_climb_rate = takeoff_climb_rate + pilot_climb_rate;
            pilot_climb_rate = 0;
        } else {
            // if overall is negative, move to pilot climb rate
            pilot_climb_rate = pilot_climb_rate + takeoff_climb_rate;
            takeoff_climb_rate = 0.0f;
        }
    } else { // pilot commands climb
        // pilot climb rate is zero until it surpasses the take-off climb rate
        if (pilot_climb_rate > takeoff_climb_rate) {
            pilot_climb_rate = pilot_climb_rate - takeoff_climb_rate;
        } else {
            pilot_climb_rate = 0.0f;
        }
    }
}

void Sub::auto_takeoff_set_start_alt(void)
{
    // start with our current altitude
    auto_takeoff_no_nav_alt_cm = inertial_nav.get_altitude();

    if (!motors.armed() || !ap.auto_armed || !motors.get_interlock() || ap.land_complete) {
        // we are not flying, add the takeoff_nav_alt
        auto_takeoff_no_nav_alt_cm += g2.takeoff_nav_alt * 100;
    }
}


/*
  call attitude controller for automatic takeoff, limiting roll/pitch
  if below takeoff_nav_alt
 */
void Sub::auto_takeoff_attitude_run(float target_yaw_rate)
{
    float nav_roll, nav_pitch;

    if (g2.takeoff_nav_alt > 0 && inertial_nav.get_altitude() < auto_takeoff_no_nav_alt_cm) {
        // we haven't reached the takeoff navigation altitude yet
        nav_roll = 0;
        nav_pitch = 0;
#if FRAME_CONFIG == HELI_FRAME
        // prevent hover roll starting till past specified altitude
        hover_roll_trim_scalar_slew = 0;
#endif
        // tell the position controller that we have limited roll/pitch demand to prevent integrator buildup
        pos_control.set_limit_accel_xy();
    } else {
        nav_roll = wp_nav.get_roll();
        nav_pitch = wp_nav.get_pitch();
    }

    // roll & pitch from waypoint controller, yaw rate from pilot
    attitude_control.input_euler_angle_roll_pitch_euler_rate_yaw(nav_roll, nav_pitch, target_yaw_rate, get_smoothing_gain());
}
