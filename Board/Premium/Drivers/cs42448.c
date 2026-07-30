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
#include "i2c.h"
#include "main.h"
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
  CS42448_REG_ADC_VOL_LAST = 0x16,
  CS42448_REG_STATUS = 0x19,
  CS42448_REG_STATUS_MASK = 0x1A
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
  CS42448_TRANSITION_SOFT_VOL = 0x43,
  CS42448_DAC_MUTE_ALL = 0xFF
};

enum
{
  CS42448_RESET_LOW_MS = 2U,
  CS42448_RESET_SETTLE_MS = 2U,
  CS42448_READY_POLL_MS = 1U,
  CS42448_READY_TIMEOUT_MS = 100U,
  CS42448_DELAY_INIT_LRCK_MS = 42U,
  CS42448_DELAY_UNMUTE_MS = 2U
};

/**
 * @brief Point d'entrée cs42448_write_reg.
 *
 * Rôle:
 * - Exécuter le traitement associé à cs42448_write_reg.
 *
 * @param addr Paramètre d'entrée de l'API.
 * @param reg Paramètre d'entrée de l'API.
 * @param value Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static HAL_StatusTypeDef cs42448_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c2,
                           (uint16_t)(addr << 1),
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           &value,
                           1U,
                           100U);
}

/**
 * @brief Point d'entrée cs42448_write_regs.
 *
 * Rôle:
 * - Exécuter le traitement associé à cs42448_write_regs.
 *
 * @param addr Paramètre d'entrée de l'API.
 * @param reg Paramètre d'entrée de l'API.
 * @param values Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static HAL_StatusTypeDef cs42448_write_regs(uint8_t addr, uint8_t reg, const uint8_t *values, uint16_t len)
{
  return HAL_I2C_Mem_Write(&hi2c2,
                           (uint16_t)(addr << 1),
                           (uint16_t)(reg | 0x80U),
                           I2C_MEMADD_SIZE_8BIT,
                           (uint8_t *)values,
                           len,
                           100U);
}

static HAL_StatusTypeDef cs42448_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
  return HAL_I2C_Mem_Read(&hi2c2,
                          (uint16_t)(addr << 1),
                          reg,
                          I2C_MEMADD_SIZE_8BIT,
                          value,
                          1U,
                          100U);
}

static cs42448_status_t cs42448_verify_reg(uint8_t addr,
                                           uint8_t reg,
                                           uint8_t expected,
                                           uint8_t mask)
{
  uint8_t value = 0U;
  if (cs42448_read_reg(addr, reg, &value) != HAL_OK)
  {
    return CS42448_STATUS_I2C;
  }
  return (((value ^ expected) & mask) == 0U) ? CS42448_STATUS_OK
                                             : CS42448_STATUS_VERIFY;
}


/**
 * @brief Point d'entrée cs42448_is_present.
 *
 * Rôle:
 * - Exécuter le traitement associé à cs42448_is_present.
 *
 * @param addr Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static bool cs42448_is_present(uint8_t addr)
{
  uint32_t elapsed = 0U;
  while (elapsed < CS42448_READY_TIMEOUT_MS)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c2, (uint16_t)(addr << 1), 1U, 2U) == HAL_OK)
    {
      return true;
    }
    HAL_Delay(CS42448_READY_POLL_MS);
    elapsed += CS42448_READY_POLL_MS;
  }
  return false;
}

/**
 * @brief Point d'entrée CS42448_Init.
 *
 * Rôle:
 * - Exécuter le traitement associé à CS42448_Init.
 *
 * @param addr Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
cs42448_status_t CS42448_Init(uint8_t addr)
{
  static const uint8_t cs42448_default_config[] = {
    CS42448_TDM_FUNCTIONAL_MODE,
    CS42448_TDM_INTERFACE_FORMAT,
    CS42448_ADC_CONTROL_SINGLE_ENDED,
    CS42448_TRANSITION_SOFT_VOL,
    CS42448_DAC_MUTE_ALL
  };

#if defined(PDN1_Pin) && defined(PDN1_GPIO_Port)
  HAL_GPIO_WritePin(PDN1_GPIO_Port, PDN1_Pin, GPIO_PIN_RESET);
#endif
#if defined(PDN2_Pin) && defined(PDN2_GPIO_Port)
  HAL_GPIO_WritePin(PDN2_GPIO_Port, PDN2_Pin, GPIO_PIN_RESET);
#endif
  HAL_Delay(CS42448_RESET_LOW_MS);
#if defined(PDN1_Pin) && defined(PDN1_GPIO_Port)
  HAL_GPIO_WritePin(PDN1_GPIO_Port, PDN1_Pin, GPIO_PIN_SET);
#endif
#if defined(PDN2_Pin) && defined(PDN2_GPIO_Port)
  HAL_GPIO_WritePin(PDN2_GPIO_Port, PDN2_Pin, GPIO_PIN_SET);
#endif
  HAL_Delay(CS42448_RESET_SETTLE_MS);

  if (!cs42448_is_present(addr))
  {
    return CS42448_STATUS_NOT_FOUND;
  }
  uint8_t chip_id = 0U;
  if (cs42448_read_reg(addr, CS42448_REG_CHIP_ID, &chip_id) != HAL_OK)
  {
    return CS42448_STATUS_I2C;
  }
  if ((chip_id & 0xF0U) != 0U)
  {
    return CS42448_STATUS_VERIFY;
  }

  /* Datasheet 4.9 step 3: set PDN=1 to hold the device in power-down. */
  if (cs42448_write_reg(addr, CS42448_REG_POWER_CTRL, CS42448_POWER_PDN_ALL) != HAL_OK)
  {
    return CS42448_STATUS_I2C;
  }
  HAL_Delay(2U);

  /* Apply Teensy CS42448 config (TDM 24-bit, single-ended ADC, soft volume, mute all). */
  if (cs42448_write_regs(addr,
                         CS42448_REG_FUNCTIONAL_MODE,
                         cs42448_default_config,
                         (uint16_t)sizeof(cs42448_default_config)) != HAL_OK)
  {
    return CS42448_STATUS_I2C;
  }
  cs42448_status_t status =
      cs42448_verify_reg(addr,
                         CS42448_REG_FUNCTIONAL_MODE,
                         CS42448_TDM_FUNCTIONAL_MODE,
                         0xFFU);
  if (status != CS42448_STATUS_OK) { return status; }
  status = cs42448_verify_reg(addr,
                              CS42448_REG_INTERFACE_FORMAT,
                              CS42448_TDM_INTERFACE_FORMAT,
                              0xFFU);
  if (status != CS42448_STATUS_OK) { return status; }
  status = cs42448_verify_reg(addr,
                              CS42448_REG_DAC_MUTE,
                              CS42448_DAC_MUTE_ALL,
                              0xFFU);
  if (status != CS42448_STATUS_OK) { return status; }

  /* Datasheet 4.9 step 6: clear PDN to power up all ADC/DAC blocks. */
  if (cs42448_write_reg(addr, CS42448_REG_POWER_CTRL, 0x00) != HAL_OK)
  {
    return CS42448_STATUS_I2C;
  }

  /* Datasheet 4.9 step 7: wait >=2000 LRCK cycles (~42 ms @48 kHz). */
  HAL_Delay(CS42448_DELAY_INIT_LRCK_MS);
  status = cs42448_verify_reg(addr, CS42448_REG_POWER_CTRL, 0x00U, 0xFFU);
  if (status != CS42448_STATUS_OK) { return status; }

  /* Unmask and reject invalid MCLK/LRCK ratios before enabling outputs. */
  if (cs42448_write_reg(addr, CS42448_REG_STATUS_MASK, 0x18U) != HAL_OK)
  {
    return CS42448_STATUS_I2C;
  }
  uint8_t clock_status = 0U;
  if (cs42448_read_reg(addr, CS42448_REG_STATUS, &clock_status) != HAL_OK)
  {
    return CS42448_STATUS_I2C;
  }
  if ((clock_status & 0x18U) != 0U)
  {
    return CS42448_STATUS_CLOCK;
  }

  /* Datasheet 4.9 step 8: wait ~90 LRCK cycles then unmute DACs. */
  HAL_Delay(CS42448_DELAY_UNMUTE_MS);
  if (cs42448_write_reg(addr, CS42448_REG_DAC_MUTE, 0x00) != HAL_OK)
  {
    return CS42448_STATUS_I2C;
  }
  status = cs42448_verify_reg(addr, CS42448_REG_DAC_MUTE, 0x00U, 0xFFU);
  if (status != CS42448_STATUS_OK) { return status; }

  return CS42448_STATUS_OK;
}
