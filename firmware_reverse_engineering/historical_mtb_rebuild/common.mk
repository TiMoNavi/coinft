################################################################################
# Shared settings for the CoinFT firmware rebuild (ModusToolbox 3.x flow).
################################################################################

MTB_TYPE=COMBINED

# Custom BSP: the stock CY8CKIT-149 BSP is a 4100S *Plus* part with a different
# interrupt table, so it cannot be reused directly for CY8C4149AZI-S593.
TARGET=APP_COINFT_S593

APPNAME=coinft_firmware_rebuild_mtb

TOOLCHAIN=GCC_ARM

# ModusToolbox's own GCC package was not installed by the setup tool, so point
# the build at a standalone Arm GNU Toolchain instead.
CY_COMPILER_GCC_ARM_DIR=/Users/550m/toolchains/arm-gnu-toolchain-13.2.Rel1-darwin-x86_64-arm-none-eabi
CY_COMPILER_PATH=$(CY_COMPILER_GCC_ARM_DIR)
CONFIG=Debug
VERBOSE=

SOURCES=
INCLUDES=./include
DEFINES=

CFLAGS=
CXXFLAGS=
ASFLAGS=
LDFLAGS=
LDLIBS=
LINKER_SCRIPT=
PREBUILD=
POSTBUILD=

COMPONENTS=
DISABLE_COMPONENTS=

# Exclude CMSIS DSP/NN/Examples — we only need Core/Include for device headers.
CY_IGNORE=    libs/cmsis-core/CMSIS/DSP     libs/cmsis-core/CMSIS/NN     libs/cmsis-core/CMSIS/CoreValidation     libs/cmsis-core/CMSIS/DoxyGen     libs/cmsis-core/CMSIS/RTOS     libs/cmsis-core/CMSIS/RTOS2     libs/cmsis-core/CMSIS/Driver     libs/cmsis-core/Device \
    libs/cmsis-core/CMSIS/Pack
