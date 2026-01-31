/**
 * @file mixer.c
 * @brief Mixeur int32 minimal 2→1 avec saturation.
 *
 * Additionne deux flux int32 et applique un clamp simple sur 32 bits.
 *
 * Rôle dans le système:
 * - Mélange basique de deux sources audio.
 *
 * Contraintes temps réel:
 * - Critique audio: oui.
 * - IRQ: non.
 * - Tasklet: oui (appelé hors IRQ).
 * - Borné: oui (boucle sur taille fixe).
 *
 * Architecture:
 * - Appelé par: audio_core.
 * - Appelle: aucun module externe.
 * - Consommé par: moteur audio.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage en IRQ.
 *
 * @note L’API publique est déclarée dans mixer.h.
 */

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
