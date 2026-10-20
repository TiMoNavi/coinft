#include "mlp_fixed.h"
#include "mlp_model.h"

#include <limits.h>
#include <stddef.h>

static int16_t activation_a[128];
static int16_t activation_b[128];

static int32_t round_shift_i64(int64_t value, uint32_t shift)
{
    const int64_t half = (int64_t)1 << (shift - 1u);
    if (value >= 0) {
        return (int32_t)((value + half) >> shift);
    }
    return -(int32_t)(((-value) + half) >> shift);
}

static int16_t saturate_i16(int32_t value, uint16_t *saturation_count)
{
    if (value > INT16_MAX) {
        ++(*saturation_count);
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        ++(*saturation_count);
        return INT16_MIN;
    }
    return (int16_t)value;
}

static void dense_relu(const int16_t *input, uint32_t input_count,
                       int16_t *output, uint32_t output_count,
                       const int8_t *weights, const int32_t *multipliers,
                       const int32_t *biases, uint16_t *saturation_count)
{
    for (uint32_t out = 0u; out < output_count; ++out) {
        int32_t accumulator = 0;
        const int8_t *row = &weights[out * input_count];
        for (uint32_t in = 0u; in < input_count; ++in) {
            accumulator += (int32_t)input[in] * (int32_t)row[in];
        }

        int32_t value = round_shift_i64(
            (int64_t)accumulator * multipliers[out], MLP_REQUANT_SHIFT);
        value += biases[out];
        if (value <= 0) {
            output[out] = 0;
        } else {
            output[out] = saturate_i16(value, saturation_count);
        }
    }
}

void coinft_mlp_infer(const int32_t zeroed[COINFT_MLP_INPUTS],
                      coinft_mlp_result_t *result)
{
    uint16_t saturation_count = 0u;

    for (uint32_t in = 0u; in < COINFT_MLP_INPUTS; ++in) {
        const int64_t normalized =
            (int64_t)zeroed[in] * mlp_input_multiplier[in] +
            mlp_input_offset[in];
        activation_a[in] = saturate_i16(
            round_shift_i64(normalized, MLP_INPUT_SHIFT),
            &saturation_count);
    }

    dense_relu(activation_a, 12u, activation_b, 128u,
               mlp_w0, mlp_m0, mlp_b0, &saturation_count);
    dense_relu(activation_b, 128u, activation_a, 64u,
               mlp_w1, mlp_m1, mlp_b1, &saturation_count);
    dense_relu(activation_a, 64u, activation_b, 36u,
               mlp_w2, mlp_m2, mlp_b2, &saturation_count);
    dense_relu(activation_b, 36u, activation_a, 24u,
               mlp_w3, mlp_m3, mlp_b3, &saturation_count);
    dense_relu(activation_a, 24u, activation_b, 12u,
               mlp_w4, mlp_m4, mlp_b4, &saturation_count);

    for (uint32_t out = 0u; out < COINFT_MLP_OUTPUTS; ++out) {
        int32_t accumulator = 0;
        const int8_t *row = &mlp_w5[out * 12u];
        for (uint32_t in = 0u; in < 12u; ++in) {
            accumulator += (int32_t)activation_b[in] * (int32_t)row[in];
        }
        result->output_micro[out] = round_shift_i64(
            (int64_t)accumulator * mlp_m5[out], MLP_OUTPUT_SHIFT) +
            mlp_b5[out];
    }
    result->saturation_count = saturation_count;
}
