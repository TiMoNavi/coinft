#ifndef COINFT_MLP_FIXED_H
#define COINFT_MLP_FIXED_H

#include <stdint.h>

#define COINFT_MLP_INPUTS  (12u)
#define COINFT_MLP_OUTPUTS (6u)
#define COINFT_MLP_OUTPUT_SCALE (1000000)

typedef struct {
    int32_t output_micro[COINFT_MLP_OUTPUTS];
    uint16_t saturation_count;
} coinft_mlp_result_t;

void coinft_mlp_infer(const int32_t zeroed[COINFT_MLP_INPUTS],
                      coinft_mlp_result_t *result);

#endif
