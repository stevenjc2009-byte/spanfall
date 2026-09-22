#ifndef UPD_CONFIG_H
#define UPD_CONFIG_H

/* Selects the per-game updater configuration. Copied from Foldwind's updater; Spanfall's CMake
 * defines UPD_GAME_SPANFALL and this copy only carries upd_config_spanfall.h. */

#if defined(UPD_GAME_SPANFALL)
#include "upd_config_spanfall.h"
#else
#error "Define UPD_GAME_SPANFALL"
#endif

/* Every config must provide these. */
#if !defined(UPD_GAME_NAME) || !defined(UPD_OWNER) || !defined(UPD_REPO) || !defined(UPD_ASSET) \
    || !defined(UPD_TITLE_ID) || !defined(UPD_DATA_DIR) || !defined(UPD_APP_DIR)                 \
    || !defined(UPD_CA_PATH) || !defined(UPD_GAME_VERSION) || !defined(UPD_LOCAL_VERSION)
#error "incomplete updater config"
#endif

#define UPD_USER_AGENT     UPD_GAME_NAME "/" UPD_GAME_VERSION " (PS Vita)"
#define UPD_MAX_DOWNLOAD   (16ULL * 1024ULL * 1024ULL)
#define UPD_MIN_FREE_BYTES (3ULL * UPD_MAX_DOWNLOAD) /* download + stage + copy */

#endif
