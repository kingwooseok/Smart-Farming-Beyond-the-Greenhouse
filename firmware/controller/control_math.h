#ifndef SMART_FARM_CONTROL_MATH_H
#define SMART_FARM_CONTROL_MATH_H

#include <stdint.h>

namespace control {
constexpr float target = 47.8f;
constexpr float trigger = 37.87f;
constexpr float clamp_pwm(float value) {
  return value < 0.0f ? 0.0f : (value > 254.0f ? 254.0f : value);
}
constexpr float next_pwm(float previous, float moisture) {
  return clamp_pwm(previous + (moisture < target ? 0.40f : 0.80f) * (target - moisture));
}
constexpr bool should_water(bool night, uint8_t moisture) {
  return !night && moisture <= trigger;
}
constexpr bool due(uint32_t now, uint32_t start, uint32_t interval) {
  return static_cast<uint32_t>(now - start) >= interval;
}
}
#endif
