#ifndef CYCFG_PERIPHERALS_H
#define CYCFG_PERIPHERALS_H

#include "cy_device_headers.h"
#include "cy_msc.h"
#include "cy_sysclk.h"

#define CYBSP_MSC0_ENABLED              (1u)
#define CYBSP_MSC1_ENABLED              (1u)
#define CY_MSC0_HW                      MSC0
#define CY_MSC1_HW                      MSC1
#define CY_MSC0_IRQ                     msc_0_interrupt_IRQn
#define CY_MSC1_IRQ                     msc_1_interrupt_IRQn

#define CY_MSC0_Cmod1_PORT              GPIO_PRT4
#define CY_MSC0_Cmod1_PIN               (0u)
#define CY_MSC0_Cmod2_PORT              GPIO_PRT4
#define CY_MSC0_Cmod2_PIN               (1u)
#define CY_MSC1_Cmod1_PORT              GPIO_PRT7
#define CY_MSC1_Cmod1_PIN               (0u)
#define CY_MSC1_Cmod2_PORT              GPIO_PRT7
#define CY_MSC1_Cmod2_PIN               (1u)

#define CY_CAPSENSE_CPU_CLK             (48000000u)
#define CY_CAPSENSE_PERI_CLK            (48000000u)
#define CY_CAPSENSE_PERI_DIV_TYPE       CY_SYSCLK_DIV_16_BIT
#define CY_CAPSENSE_PERI_DIV_INDEX      (0u)
#define CY_MSC_CH_NUMBER                (2u)
#define CY_MSC_ENABLED_CH_NUMBER        (2u)
#define CY_MSC_SENSE_MODE_NUMBER        (3u)
#define CY_MSC_CHANNEL_OFFSET           (0u)
#define CY_CAPSENSE_MSC0_EN             (1u)
#define CY_CAPSENSE_MSC1_EN             (1u)

#endif
