#ifndef CYCFG_CAPSENSE_H
#define CYCFG_CAPSENSE_H

#include "cy_device_headers.h"
#include "cycfg_capsense_defines.h"
#include "cy_capsense.h"

#include <stdbool.h>

#define COINFT_WIDGET_COUNT          (13u)
#define COINFT_OUTPUT_CHANNEL_COUNT  (12u)
#define COINFT_REFERENCE_WIDGET_ID   (12u)
#define COINFT_REFERENCE_SLOT_ID     (6u)

extern cy_stc_capsense_context_t cy_capsense_context;
extern const cy_stc_capsense_electrode_config_t coinft_reference_electrode;

void coinft_capsense_load_factory_frames(void);
void coinft_capsense_prepare_frames(void);
bool coinft_capsense_apply_parameters(uint32_t channel,
                                      uint8_t cdac_ref,
                                      uint8_t cdac_comp);
uint16_t coinft_capsense_raw(uint32_t channel);

#endif
