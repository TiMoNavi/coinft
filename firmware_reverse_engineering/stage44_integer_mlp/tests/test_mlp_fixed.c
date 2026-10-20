#include "golden_vectors.h"
#include "mlp_fixed.h"

#include <stdio.h>

int main(void)
{
    for (unsigned int vector = 0u; vector < GOLDEN_VECTOR_COUNT; ++vector) {
        coinft_mlp_result_t result;
        coinft_mlp_infer(golden_input[vector], &result);
        if (result.saturation_count != golden_saturation[vector]) {
            fprintf(stderr, "vector %u saturation: %u != %u\n", vector,
                    result.saturation_count, golden_saturation[vector]);
            return 1;
        }
        for (unsigned int axis = 0u; axis < COINFT_MLP_OUTPUTS; ++axis) {
            if (result.output_micro[axis] != golden_output[vector][axis]) {
                fprintf(stderr, "vector %u axis %u: %ld != %ld\n",
                        vector, axis, (long)result.output_micro[axis],
                        (long)golden_output[vector][axis]);
                return 1;
            }
        }
    }
    printf("PASS %u C fixed-point golden vectors\n", GOLDEN_VECTOR_COUNT);
    return 0;
}
