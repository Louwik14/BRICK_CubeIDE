/**
 * @file app_sample_boot.c
 * @brief Module applicatif app_sample_boot.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à app_sample_boot.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "App/app_sample_boot.h"
#include "Storage/wav_loader.h"

/**
 * @brief Point d'entrée app_sample_boot_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à app_sample_boot_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void app_sample_boot_init(void)
{
    char wav_path[64];
    (void)wav_loader_find_first_wav(wav_path, sizeof(wav_path));
}
