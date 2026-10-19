#pragma once

#include <cstddef>

namespace coinft {

struct DenseLayer {
  const float* weights;
  const float* bias;
  unsigned int input_size;
  unsigned int output_size;
};

struct Model {
  const char* side;
  const float* mu_x;
  const float* sd_x;
  const float* mu_y;
  const float* sd_y;
  const DenseLayer* layers;
  unsigned int layer_count;
};

const Model& leftModel();
const Model& rightModel();
bool infer(const Model& model, const float raw[12], float wrench[6]);

}  // namespace coinft
