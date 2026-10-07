#include "../firmware/controller/control_math.h"

static_assert(control::should_water(false, 37), "Dry daylight starts irrigation");
static_assert(!control::should_water(false, 38), "Moisture above trigger blocks irrigation");
static_assert(!control::should_water(true, 0), "Night blocks even dry soil");
static_assert(control::next_pwm(150.0f, 30.0f) > 157.11f &&
              control::next_pwm(150.0f, 30.0f) < 157.13f, "Dry correction is +7.12");
static_assert(control::next_pwm(150.0f, 70.0f) > 132.23f &&
              control::next_pwm(150.0f, 70.0f) < 132.25f, "Wet correction is -17.76");
static_assert(control::next_pwm(150.0f, 47.8f) == 150.0f, "At target keep output");
static_assert(control::next_pwm(253.0f, 0.0f) == 254.0f, "Upper output clamp");
static_assert(control::next_pwm(1.0f, 100.0f) == 0.0f, "Lower output clamp");
static_assert(!control::due(304, 0, 305), "Do not update early");
static_assert(control::due(306, 0, 305), "Do not miss a delayed loop");
static_assert(control::due(4, 0xFFFFFFFEUL, 6), "Timer wraparound");
