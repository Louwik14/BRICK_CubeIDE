/**
 * @file cs42448.c
 * @brief Initialisation du codec audio CS42448 via I2C.
 *
 * Ce module configure les registres du CS42448 (format TDM,
 * volumes, alimentation) lors du démarrage du système.
 *
 * Rôle dans le système:
 * - Configuration du codec audio avant le démarrage DMA SAI.
 * - Assure un état connu des ADC/DAC.
 *
 * Contraintes temps réel:
 * - Critique audio: non (initialisation seulement).
 * - Tasklet: non.
 * - IRQ: non.
 * - Borné: non critique (I2C bloquant possible).
 *
 * Architecture:
 * - Appelé par: séquence d'initialisation applicative.
 * - Appelle: HAL I2C (mem write).
 *
 * Règles:
 * - Pas de malloc.
 * - Ne pas appeler en IRQ.
 *
 * @note L’API publique est déclarée dans cs42448.h.
 */

#include "cs42448.h"
#include "diagnostics_tasklet.h"
#include "i2c.h"
#include <stddef.h>
#include <stdbool.h>

/* CS42448 register map (subset used). */
enum
{
  CS42448_REG_CHIP_ID = 0x01,
  CS42448_REG_POWER_CTRL = 0x02,
  CS42448_REG_FUNCTIONAL_MODE = 0x03,
  CS42448_REG_INTERFACE_FORMAT = 0x04,
  CS42448_REG_ADC_CTRL = 0x05,
  CS42448_REG_TRANSITION_CTRL = 0x06,
  CS42448_REG_DAC_MUTE = 0x07,
  CS42448_REG_DAC_VOL_BASE = 0x08,
  CS42448_REG_DAC_VOL_LAST = 0x0F,
  CS42448_REG_ADC_VOL_BASE = 0x11,
  CS42448_REG_ADC_VOL_LAST = 0x16
};

enum
{
  CS42448_POWER_PDN_ALL = 0xFF
};

enum
{
  CS42448_TDM_FUNCTIONAL_MODE = 0xF4,
  CS42448_TDM_INTERFACE_FORMAT = 0x76,
  CS42448_ADC_CONTROL_SINGLE_ENDED = 0x1C,
  CS42448_TRANSITION_SOFT_VOL = 0x63,
  CS42448_DAC_MUTE_ALL = 0xFF
};

enum
{
  CS42448_DELAY_INIT_LRCK_MS = 50U,
  CS42448_DELAY_UNMUTE_MS = 2U
};

static HAL_StatusTypeDef cs42448_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c1,
                           (uint16_t)(addr << 1),
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           &value,
                           1U,
                           100U);
}

static HAL_StatusTypeDef cs42448_write_regs(uint8_t addr, uint8_t reg, const uint8_t *values, uint16_t len)
{
  return HAL_I2C_Mem_Write(&hi2c1,
                           (uint16_t)(addr << 1),
                           (uint16_t)(reg | 0x80U),
                           I2C_MEMADD_SIZE_8BIT,
                           (uint8_t *)values,
                           len,
                           100U);
}

static HAL_StatusTypeDef cs42448_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
  return HAL_I2C_Mem_Read(&hi2c1,
                          (uint16_t)(addr << 1),
                          reg,
                          I2C_MEMADD_SIZE_8BIT,
                          value,
                          1U,
                          100U);
}

static bool cs42448_is_present(uint8_t addr)
{
  return HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 3U, 100U) == HAL_OK;
}

bool CS42448_Init(uint8_t addr)
{
  static const uint8_t cs42448_default_config[] = {
    CS42448_TDM_FUNCTIONAL_MODE,
    CS42448_TDM_INTERFACE_FORMAT,
    CS42448_ADC_CONTROL_SINGLE_ENDED,
    CS42448_TRANSITION_SOFT_VOL,
    CS42448_DAC_MUTE_ALL
  };

  if (!cs42448_is_present(addr))
  {
    return false;
  }

  /* Datasheet 4.9 step 3: set PDN=1 to hold the device in power-down. */
  if (cs42448_write_reg(addr, CS42448_REG_POWER_CTRL, CS42448_POWER_PDN_ALL) != HAL_OK)
  {
    return false;
  }
  HAL_Delay(2U);

  /* Apply Teensy CS42448 config (TDM 24-bit, single-ended ADC, soft volume, mute all). */
  if (cs42448_write_regs(addr,
                         CS42448_REG_FUNCTIONAL_MODE,
                         cs42448_default_config,
                         (uint16_t)sizeof(cs42448_default_config)) != HAL_OK)
  {
    return false;
  }

  /* Datasheet 4.9 step 6: clear PDN to power up all ADC/DAC blocks. */
  if (cs42448_write_reg(addr, CS42448_REG_POWER_CTRL, 0x00) != HAL_OK)
  {
    return false;
  }

  /* Datasheet 4.9 step 7: wait >=2000 LRCK cycles (~42 ms @48 kHz). */
  HAL_Delay(CS42448_DELAY_INIT_LRCK_MS);

  /* Datasheet 4.9 step 8: wait ~90 LRCK cycles then unmute DACs. */
  HAL_Delay(CS42448_DELAY_UNMUTE_MS);
  if (cs42448_write_reg(addr, CS42448_REG_DAC_MUTE, 0x00) != HAL_OK)
  {
    return false;
  }

  return true;
}

void CS42448_DiagnosticsDump(uint8_t addr)
{
  uint8_t value = 0U;
  HAL_StatusTypeDef status = HAL_OK;

  diagnostics_logf("\r\n==== CS42448 DIAGNOSTICS DUMP ====\r\n");

  /* ------------------------------------------------------------
     Core registers
     ------------------------------------------------------------ */
  struct
  {
    uint8_t reg;
    const char *label;
  } const core_regs[] = {
      {0x01, "CHIP_ID"},
      {0x02, "POWER_CTRL"},
      {0x03, "FUNCTIONAL_MODE"},
      {0x04, "INTERFACE_FORMAT"},
      {0x05, "ADC_CTRL"},
      {0x06, "TRANSITION_CTRL"},
      {0x07, "DAC_MUTE"}};

  for (size_t i = 0; i < (sizeof(core_regs) / sizeof(core_regs[0])); i++)
  {
    status = cs42448_read_reg(addr, core_regs[i].reg, &value);
    if (status == HAL_OK)
    {
      diagnostics_logf("[CS42448] %-18s (0x%02X) = 0x%02X\r\n",
                       core_regs[i].label,
                       core_regs[i].reg,
                       value);
    }
    else
    {
      diagnostics_logf("[CS42448] ERROR reading reg 0x%02X\r\n",
                       core_regs[i].reg);
    }
  }

  /* ------------------------------------------------------------
     DAC volumes
     ------------------------------------------------------------ */
  diagnostics_logf("\r\n-- DAC VOLUMES --\r\n");

  for (uint8_t reg = 0x08; reg <= 0x0F; reg++)
  {
    status = cs42448_read_reg(addr, reg, &value);
    if (status == HAL_OK)
    {
      diagnostics_logf("[CS42448] DAC_VOL[%u] (0x%02X) = 0x%02X\r\n",
                       (unsigned int)(reg - 0x08 + 1U),
                       reg,
                       value);
    }
  }

  /* ------------------------------------------------------------
     ADC volumes
     ------------------------------------------------------------ */
  diagnostics_logf("\r\n-- ADC VOLUMES --\r\n");

  for (uint8_t reg = 0x11; reg <= 0x16; reg++)
  {
    status = cs42448_read_reg(addr, reg, &value);
    if (status == HAL_OK)
    {
      diagnostics_logf("[CS42448] ADC_VOL[%u] (0x%02X) = 0x%02X\r\n",
                       (unsigned int)(reg - 0x11 + 1U),
                       reg,
                       value);
    }
  }

  /* ------------------------------------------------------------
     FULL RAW REGISTER DUMP (most important for DAC3–8 debug)
     ------------------------------------------------------------ */
  diagnostics_logf("\r\n-- RAW REGISTER DUMP 0x00–0x30 --\r\n");

  for (uint8_t reg = 0x00; reg <= 0x30; reg++)
  {
    status = cs42448_read_reg(addr, reg, &value);
    if (status == HAL_OK)
    {
      diagnostics_logf("REG[0x%02X] = 0x%02X\r\n", reg, value);
    }
    else
    {
      diagnostics_logf("REG[0x%02X] = ERROR\r\n", reg);
    }
  }

  diagnostics_logf("==== END CS42448 DUMP ====\r\n\r\n");
}
