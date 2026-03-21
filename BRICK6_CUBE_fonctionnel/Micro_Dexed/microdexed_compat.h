#pragma once

#ifdef MICRODEXED_MINIMAL

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

using byte = uint8_t;

template <typename T, typename U, typename V>
constexpr auto constrain(const T& value, const U& min_value, const V& max_value)
    -> std::common_type_t<T, U, V>
{
  using common_t = std::common_type_t<T, U, V>;
  const common_t converted_value = static_cast<common_t>(value);
  const common_t converted_min = static_cast<common_t>(min_value);
  const common_t converted_max = static_cast<common_t>(max_value);

  return converted_value < converted_min ? converted_min : (converted_value > converted_max ? converted_max : converted_value);
}

inline uint32_t millis()
{
  using clock = std::chrono::steady_clock;
  static const auto start = clock::now();
  const auto now = clock::now();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

inline int32_t signed_saturate_rshift(int32_t value, int bits, int rshift)
{
  const int32_t shifted = value >> rshift;
  const int32_t max_value = (1 << (bits - 1)) - 1;
  const int32_t min_value = -(1 << (bits - 1));
  return constrain(shifted, min_value, max_value);
}

inline void arm_float_to_q15(const float *src, int16_t *dst, uint32_t block_size)
{
  for (uint32_t i = 0; i < block_size; ++i)
  {
    const float scaled = src[i] * 32768.0f;
    const float clamped = constrain(scaled, -32768.0f, 32767.0f);
    dst[i] = static_cast<int16_t>(std::lrint(clamped));
  }
}

#else

#include <Arduino.h>
#include <Audio.h>

#endif
