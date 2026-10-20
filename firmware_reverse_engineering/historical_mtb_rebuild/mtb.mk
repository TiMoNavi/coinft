# MTB项目配置
APPNAME=coinft_firmware_rebuild_mtb
LIBNAME=
TARGET=CY8CKIT-149
CORE=CM0P
CORE_NAME=CM0P_0

# 需要的组件
COMPONENTS=CAT2 BSP_DESIGN_MODUS

# 禁用的组件
DISABLE_COMPONENTS=

# 依赖
DEPS=$(TARGET)
DEPS+=mtb-pdl-cat2
DEPS+=mtb-hal-cat2
DEPS+=core-lib
