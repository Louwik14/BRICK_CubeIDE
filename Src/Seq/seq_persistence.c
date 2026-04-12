/*
 * Module: seq_persistence
 * Role: Persistance SD du projet séquenceur (format binaire versionné).
 * Responsibilities: mount/IO fichier, en-tête magic/version, CRC16,
 * chargement/sauvegarde atomiques de l'état exposé par seq_model.
 * Integration: frontière stockage du séquenceur; ne contient aucune logique d'édition/playback.
 */
#include "Seq/seq_persistence.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "SD/bsp_driver_sd.h"

#include "Seq/seq_model.h"

#define SEQ_FILE_MAGIC        0x42534551UL /* 'BSEQ' */
#define SEQ_FILE_VERSION      2U
#define SEQ_FILE_PATH         "0:/BRICK6/seq_v1.bin"
#define SEQ_STORAGE_DIR       "0:/BRICK6"

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_sz;
    uint16_t crc16;
    uint16_t reserved;
} seq_file_header_t;

static FATFS g_seq_fs;
static uint8_t g_seq_fs_mounted = 0U;

static uint16_t seq_crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;

    if (data == 0)
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < len; ++i)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (uint8_t b = 0U; b < 8U; ++b)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static uint8_t seq_persistence_mount_if_needed(void)
{
    if (g_seq_fs_mounted != 0U)
    {
        return 1U;
    }

    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
        printf("[SEQ][PERSIST] sd absent\r\n");
        return 0U;
    }

    if (f_mount(&g_seq_fs, "0:", 1U) != FR_OK)
    {
        printf("[SEQ][PERSIST] mount failed\r\n");
        return 0U;
    }

    g_seq_fs_mounted = 1U;
    return 1U;
}

uint8_t seq_persistence_load(void)
{
    FIL fp;
    FRESULT fr;
    UINT br = 0U;
    seq_file_header_t header;
    seq_project_data_t payload;

    if (seq_persistence_mount_if_needed() == 0U)
    {
        seq_model_init_defaults();
        return 0U;
    }

    fr = f_open(&fp, SEQ_FILE_PATH, FA_READ);
    if (fr != FR_OK)
    {
        printf("[SEQ][PERSIST] no file, defaults\r\n");
        seq_model_init_defaults();
        return 0U;
    }

    memset(&header, 0, sizeof(header));
    fr = f_read(&fp, &header, sizeof(header), &br);
    if ((fr != FR_OK) || (br != sizeof(header)))
    {
        printf("[SEQ][PERSIST] read header failed\r\n");
        (void)f_close(&fp);
        seq_model_init_defaults();
        return 0U;
    }

    if ((header.magic != SEQ_FILE_MAGIC) ||
        (header.version != SEQ_FILE_VERSION) ||
        (header.payload_sz != (uint16_t)sizeof(payload)))
    {
        printf("[SEQ][PERSIST] invalid header\r\n");
        (void)f_close(&fp);
        seq_model_init_defaults();
        return 0U;
    }

    memset(&payload, 0, sizeof(payload));
    fr = f_read(&fp, &payload, sizeof(payload), &br);
    (void)f_close(&fp);

    if ((fr != FR_OK) || (br != sizeof(payload)))
    {
        printf("[SEQ][PERSIST] read payload failed\r\n");
        seq_model_init_defaults();
        return 0U;
    }

    if (seq_crc16_ccitt((const uint8_t *)&payload, sizeof(payload)) != header.crc16)
    {
        printf("[SEQ][PERSIST] crc mismatch\r\n");
        seq_model_init_defaults();
        return 0U;
    }

    if (seq_model_load_project(&payload) == 0U)
    {
        printf("[SEQ][PERSIST] payload rejected\r\n");
        seq_model_init_defaults();
        return 0U;
    }

    printf("[SEQ][PERSIST] load ok\r\n");
    return 1U;
}

uint8_t seq_persistence_save(void)
{
    FIL fp;
    FRESULT fr;
    UINT bw = 0U;
    seq_file_header_t header;
    const seq_project_data_t *project = seq_model_get_project();

    if (project == 0)
    {
        return 0U;
    }

    if (seq_persistence_mount_if_needed() == 0U)
    {
        return 0U;
    }

    fr = f_mkdir(SEQ_STORAGE_DIR);
    if ((fr != FR_OK) && (fr != FR_EXIST))
    {
        printf("[SEQ][PERSIST] mkdir failed: %u\r\n", (unsigned)fr);
        return 0U;
    }

    header.magic = SEQ_FILE_MAGIC;
    header.version = SEQ_FILE_VERSION;
    header.payload_sz = (uint16_t)sizeof(*project);
    header.crc16 = seq_crc16_ccitt((const uint8_t *)project, sizeof(*project));
    header.reserved = 0U;

    fr = f_open(&fp, SEQ_FILE_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        printf("[SEQ][PERSIST] open for write failed: %u\r\n", (unsigned)fr);
        return 0U;
    }

    fr = f_write(&fp, &header, sizeof(header), &bw);
    if ((fr != FR_OK) || (bw != sizeof(header)))
    {
        printf("[SEQ][PERSIST] write header failed\r\n");
        (void)f_close(&fp);
        return 0U;
    }

    fr = f_write(&fp, project, sizeof(*project), &bw);
    if ((fr != FR_OK) || (bw != sizeof(*project)))
    {
        printf("[SEQ][PERSIST] write payload failed\r\n");
        (void)f_close(&fp);
        return 0U;
    }

    fr = f_sync(&fp);
    (void)f_close(&fp);
    if (fr != FR_OK)
    {
        printf("[SEQ][PERSIST] sync failed\r\n");
        return 0U;
    }

    printf("[SEQ][PERSIST] save ok\r\n");
    return 1U;
}
