#include "mixer.h"

#include <limits.h>

void mixer_mix_2_to_1(const int32_t *in_a,
                      const int32_t *in_b,
                      int32_t *out,
                      uint32_t samples)
{
  if ((in_a == NULL) || (in_b == NULL) || (out == NULL))
  {
    return;
  }

  for (uint32_t i = 0; i < samples; ++i)
  {
    int64_t sum = (int64_t)in_a[i] + (int64_t)in_b[i];
    if (sum > INT32_MAX)
    {
      out[i] = INT32_MAX;
    }
    else if (sum < INT32_MIN)
    {
      out[i] = INT32_MIN;
    }
    else
    {
      out[i] = (int32_t)sum;
    }
  }
}
