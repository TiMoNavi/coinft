#ifndef COINFT_MLP_MODEL_H
#define COINFT_MLP_MODEL_H

#include <stdint.h>

#define MLP_REQUANT_SHIFT (24u)
#define MLP_INPUT_SHIFT   (8u)
#define MLP_OUTPUT_SHIFT  (20u)

extern const int32_t mlp_input_multiplier[12];
extern const int32_t mlp_input_offset[12];

extern const int8_t mlp_w0[128 * 12];
extern const int32_t mlp_m0[128];
extern const int32_t mlp_b0[128];
extern const int8_t mlp_w1[64 * 128];
extern const int32_t mlp_m1[64];
extern const int32_t mlp_b1[64];
extern const int8_t mlp_w2[36 * 64];
extern const int32_t mlp_m2[36];
extern const int32_t mlp_b2[36];
extern const int8_t mlp_w3[24 * 36];
extern const int32_t mlp_m3[24];
extern const int32_t mlp_b3[24];
extern const int8_t mlp_w4[12 * 24];
extern const int32_t mlp_m4[12];
extern const int32_t mlp_b4[12];
extern const int8_t mlp_w5[6 * 12];
extern const int32_t mlp_m5[6];
extern const int32_t mlp_b5[6];

#endif
