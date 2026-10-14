#include "sim_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PNG_WIDTH 128U
#define PNG_HEIGHT 64U

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    crc = ~crc;
    for (size_t i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

static uint32_t adler32(const uint8_t *data, size_t length)
{
    uint32_t a = 1U;
    uint32_t b = 0U;
    for (size_t i = 0U; i < length; ++i)
    {
        a = (a + data[i]) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

static int put_u32(FILE *file, uint32_t value)
{
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24U), (uint8_t)(value >> 16U),
        (uint8_t)(value >> 8U), (uint8_t)value
    };
    return fwrite(bytes, 1U, sizeof(bytes), file) == sizeof(bytes);
}

static int write_chunk(FILE *file, const char type[4],
                       const uint8_t *data, size_t length)
{
    uint32_t crc = crc32_update(0U, (const uint8_t *)type, 4U);
    if (length > 0U) crc = crc32_update(crc, data, length);
    return put_u32(file, (uint32_t)length)
        && (fwrite(type, 1U, 4U, file) == 4U)
        && ((length == 0U) || (fwrite(data, 1U, length, file) == length))
        && put_u32(file, crc);
}

int sim_png_write_framebuffer(const char *path, const uint8_t *framebuffer,
                              size_t framebuffer_size, char *error,
                              size_t error_size)
{
    enum { RAW_SIZE = (PNG_WIDTH + 1U) * PNG_HEIGHT };
    uint8_t *raw = NULL;
    uint8_t *zlib = NULL;
    FILE *file = NULL;
    int success = 0;

    if ((framebuffer == NULL) || (framebuffer_size != 1024U))
    {
        (void)snprintf(error, error_size, "framebuffer must contain exactly 1024 bytes");
        return 0;
    }

    raw = (uint8_t *)malloc(RAW_SIZE);
    zlib = (uint8_t *)malloc(RAW_SIZE + 11U);
    if ((raw == NULL) || (zlib == NULL))
    {
        (void)snprintf(error, error_size, "out of memory while encoding PNG");
        goto cleanup;
    }

    for (unsigned y = 0U; y < PNG_HEIGHT; ++y)
    {
        raw[y * (PNG_WIDTH + 1U)] = 0U;
        for (unsigned x = 0U; x < PNG_WIDTH; ++x)
        {
            const uint8_t byte = framebuffer[(y / 8U) * PNG_WIDTH + x];
            raw[y * (PNG_WIDTH + 1U) + 1U + x] =
                ((byte >> (y & 7U)) & 1U) ? 255U : 0U;
        }
    }

    size_t z = 0U;
    zlib[z++] = 0x78U;
    zlib[z++] = 0x01U;
    zlib[z++] = 0x01U;
    zlib[z++] = (uint8_t)(RAW_SIZE & 0xFFU);
    zlib[z++] = (uint8_t)(RAW_SIZE >> 8U);
    const uint16_t inverted = (uint16_t)~(uint16_t)RAW_SIZE;
    zlib[z++] = (uint8_t)(inverted & 0xFFU);
    zlib[z++] = (uint8_t)(inverted >> 8U);
    memcpy(zlib + z, raw, RAW_SIZE);
    z += RAW_SIZE;
    const uint32_t adler = adler32(raw, RAW_SIZE);
    zlib[z++] = (uint8_t)(adler >> 24U);
    zlib[z++] = (uint8_t)(adler >> 16U);
    zlib[z++] = (uint8_t)(adler >> 8U);
    zlib[z++] = (uint8_t)adler;

    file = fopen(path, "wb");
    if (file == NULL)
    {
        (void)snprintf(error, error_size, "could not open capture file");
        goto cleanup;
    }
    static const uint8_t signature[8] = {137U,80U,78U,71U,13U,10U,26U,10U};
    const uint8_t ihdr[13] = {
        0U,0U,0U,128U, 0U,0U,0U,64U, 8U,0U,0U,0U,0U
    };
    if ((fwrite(signature, 1U, sizeof(signature), file) != sizeof(signature))
            || !write_chunk(file, "IHDR", ihdr, sizeof(ihdr))
            || !write_chunk(file, "IDAT", zlib, z)
            || !write_chunk(file, "IEND", NULL, 0U)
            || (fclose(file) != 0))
    {
        file = NULL;
        (void)snprintf(error, error_size, "failed while writing PNG data");
        goto cleanup;
    }
    file = NULL;
    success = 1;

cleanup:
    if (file != NULL) (void)fclose(file);
    free(zlib);
    free(raw);
    return success;
}

