#ifndef UPD_CONFIG_SPANFALL_H
#define UPD_CONFIG_SPANFALL_H

#include "version.h" /* SF_VERSION */

#define UPD_GAME_NAME    "Spanfall"
#define UPD_OWNER        "stevenjc2009-byte"
#define UPD_REPO         "spanfall"
#define UPD_ASSET        "spanfall.vpk"
#define UPD_TITLE_ID     "SPNF00001"
#define UPD_DATA_DIR     "ux0:data/Spanfall/update"
#define UPD_APP_DIR      "ux0:app/SPNF00001"
#define UPD_CA_PATH      "app0:assets/cacert.pem"
#define UPD_GAME_VERSION SF_VERSION

/* Test builds only: -DSF_UPDATER_PRETEND_VERSION="\"0.0.0\"" makes the updater (and nothing
 * else) believe it is older, so a check finds the published release. Never in a release. */
#ifdef SF_UPDATER_PRETEND_VERSION
#define UPD_LOCAL_VERSION SF_UPDATER_PRETEND_VERSION
#else
#define UPD_LOCAL_VERSION SF_VERSION
#endif

#endif
