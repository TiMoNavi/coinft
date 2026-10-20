################################################################################
# CoinFT V2 firmware rebuild - top-level Makefile (MTB 3.x flow)
#
# Target: CY8C4149AZI-S593 (PSoC 4100S Max, 384 KB flash, 32 KB SRAM)
# Device parameters come from Infineon's MPN database (OpenOCD cympn.cfg entry
# 2E2B) and were confirmed on the live silicon over SWD.
################################################################################

include ./common.mk

CY_GETLIBS_SHARED_PATH=../
CY_GETLIBS_SHARED_NAME=mtb_shared
CY_GETLIBS_PATH=./libs

# Library locations.  These were cloned manually because project-creator could
# not reach GitHub; the v3 flow needs one SEARCH_ variable per asset.
SEARCH_TARGET_APP_COINFT_S593=./bsps/TARGET_APP_COINFT_S593
SEARCH_core-make=./libs/core-make
SEARCH_recipe-make-cat2=./libs/recipe-make-cat2
SEARCH_mtb-pdl-cat2=./libs/mtb-pdl-cat2
SEARCH_mtb-hal-cat2=./libs/mtb-hal-cat2
SEARCH_core-lib=./libs/core-lib
SEARCH_cmsis-core=./libs/cmsis-core

SEARCH=\
    ./bsps/TARGET_APP_COINFT_S593 \
    ./libs/core-make \
    ./libs/recipe-make-cat2 \
    ./libs/mtb-pdl-cat2 \
    ./libs/mtb-hal-cat2 \
    ./libs/core-lib \
    ./libs/cmsis-core/CMSIS/Core/Include

CY_TOOLS_PATHS?=/Applications/ModusToolbox/tools_3.7
CY_TOOLS_DIR?=$(CY_TOOLS_PATHS)

include $(CY_TOOLS_DIR)/make/start.mk
