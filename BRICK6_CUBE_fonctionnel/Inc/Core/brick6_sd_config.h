#ifndef BRICK6_SD_CONFIG_H
#define BRICK6_SD_CONFIG_H

/*
 * Mettre à 1 pour les campagnes de dev PATTERN/SD:
 * - désactive recorder SD
 * - désactive chargement sample boot SD
 * - réduit le timeout diskio pour éviter les gels longs silencieux
 */
#ifndef BRICK6_SD_DEV_SAFE_MODE
#define BRICK6_SD_DEV_SAFE_MODE 1
#endif

/*
 * Interrupteur de debug ciblé: neutralise temporairement la maintenance D-cache
 * sur le chemin SD/FatFs pour isoler les causes save/load sans toucher au D-cache global.
 */
#ifndef BRICK6_SD_ENABLE_DMA_CACHE_MAINTENANCE
#define BRICK6_SD_ENABLE_DMA_CACHE_MAINTENANCE 0
#endif

#if BRICK6_SD_DEV_SAFE_MODE
#define BRICK6_SD_ENABLE_RECORDER          0
#define BRICK6_SD_ENABLE_BOOT_SAMPLE_LOAD  0
#define BRICK6_SD_TIMEOUT_MS               3000U
#else
#define BRICK6_SD_ENABLE_RECORDER          1
#define BRICK6_SD_ENABLE_BOOT_SAMPLE_LOAD  1
#define BRICK6_SD_TIMEOUT_MS               10000U
#endif

#endif
