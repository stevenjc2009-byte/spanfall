cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(spanfall C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Spanfall")
set(VITA_TITLEID  "SPNF00001")

# VITA_VERSION is derived from SF_VERSION in src/version.h (single source of truth).
# Mapping: SF_VERSION "MAJOR.MINOR.PATCH" -> param.sfo APP_VER "MM.PP", where
#   MM = two-digit MAJOR,  PP = two-digit (MINOR*10 + PATCH).
#   1.0.0 -> "01.00"   1.0.1 -> "01.01"   1.2.0 -> "01.20"
# APP_VER only has two 2-digit fields, so MINOR and PATCH must each stay <= 9 under this rule;
# configure fails loudly otherwise instead of silently producing a wrong/colliding version.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" SF_VERSION_LINE REGEX "^#define SF_VERSION ")
if(NOT SF_VERSION_LINE MATCHES "\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
  message(FATAL_ERROR "Could not parse SF_VERSION out of src/version.h")
endif()
set(SF_VER_MAJOR "${CMAKE_MATCH_1}")
set(SF_VER_MINOR "${CMAKE_MATCH_2}")
set(SF_VER_PATCH "${CMAKE_MATCH_3}")
if(SF_VER_MAJOR GREATER 99 OR SF_VER_MINOR GREATER 9 OR SF_VER_PATCH GREATER 9)
  message(FATAL_ERROR "SF_VERSION ${SF_VER_MAJOR}.${SF_VER_MINOR}.${SF_VER_PATCH} does not fit APP_VER MM.PP (MINOR and PATCH must be 0-9)")
endif()
math(EXPR SF_VER_PP "${SF_VER_MINOR} * 10 + ${SF_VER_PATCH}")
if(SF_VER_MAJOR LESS 10)
  set(SF_VER_MAJOR "0${SF_VER_MAJOR}")
endif()
if(SF_VER_PP LESS 10)
  set(SF_VER_PP "0${SF_VER_PP}")
endif()
set(VITA_VERSION "${SF_VER_MAJOR}.${SF_VER_PP}")
message(STATUS "VITA_VERSION (derived from SF_VERSION in src/version.h): ${VITA_VERSION}")

# Emulator/test builds only: makes the in-game updater (and nothing else) believe the installed
# version is this one, so a check against the live repo finds the published release. Empty means
# the real SF_VERSION. Never set this in a release build.
set(SF_UPDATER_PRETEND_VERSION "" CACHE STRING "Emulator-only: version the updater treats as installed")

# The updater's URL handling is only the code that was tested if libcurl was built without
# HAVE_INET_PTON. Check the archive at configure time rather than discovering it at link time.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/upd_curl_guard.cmake")
upd_check_curl_inet_pton()

file(GLOB SF_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB SF_UPDATER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/updater/*.c")

add_executable(spanfall ${SF_SOURCES} ${SF_UPDATER_SOURCES})
target_include_directories(spanfall PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# optimisation comes from CMAKE_BUILD_TYPE (build.sh passes Release: -O3)
target_compile_options(spanfall PRIVATE -Wall -Wextra)
target_compile_definitions(spanfall PRIVATE UPD_GAME_SPANFALL)
if(NOT SF_UPDATER_PRETEND_VERSION STREQUAL "")
  message(WARNING "SF_UPDATER_PRETEND_VERSION=${SF_UPDATER_PRETEND_VERSION}: test build, never release this VPK")
  target_compile_definitions(spanfall PRIVATE
    "SF_UPDATER_PRETEND_VERSION=\"${SF_UPDATER_PRETEND_VERSION}\"")
endif()

target_link_libraries(spanfall
  # graphics / fonts
  vita2d
  freetype
  png16
  jpeg
  bz2

  # updater: curl before ssl/crypto/z/zstd, the order in
  # ${VITASDK}/arm-vita-eabi/lib/pkgconfig/libcurl.pc (Libs: -lcurl ... -lssl -lcrypto -lz -lzstd).
  # Without zstd: undefined ZSTD_decompressStream (curl's content_encoding.c).
  curl
  ssl
  crypto
  z
  zstd
  m

  # system stubs (after vita2d: it needs SceAppMgr_stub for sceSharedFb*/GetBudgetInfo;
  # SceAppMgr_stub is also what provides sceAppMgrLoadExec for the updater's restart)
  SceAppMgr_stub
  SceDisplay_stub
  SceGxm_stub
  SceCtrl_stub
  SceSysmodule_stub
  SceCommonDialog_stub
  SceAppUtil_stub
  ScePgf_stub
  ScePvf_stub
  ScePower_stub
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
  SceLibKernel_stub

  # updater stubs (network, module load, process/rtc/sysmem used by curl + OpenSSL)
  SceNet_stub
  SceNetCtl_stub
  SceKernelModulemgr_stub
  SceProcessmgr_stub
  SceRtc_stub
  SceSysmem_stub

  pthread # VitaSDK libcrypto's threads_pthread.o needs pthread_rwlock_* and friends
  c
)

# UNSAFE: the updater writes into ux0:app/SPNF00001 and calls sceAppMgrLoadExec.
vita_create_self(eboot.bin spanfall UNSAFE)

vita_create_vpk(spanfall.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  # must match UPD_CA_PATH in src/updater/upd_config_spanfall.h
  FILE assets/cacert.pem assets/cacert.pem
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
