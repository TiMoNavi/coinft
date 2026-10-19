#include "inference.h"

#include <cmath>

#include "model_params.h"

namespace coinft {
namespace {

#define COINFT_LAYERS(side)                                                    \
  {coinft_models::side##_w0, coinft_models::side##_b0, 12, 128},              \
      {coinft_models::side##_w1, coinft_models::side##_b1, 128, 64},           \
      {coinft_models::side##_w2, coinft_models::side##_b2, 64, 36},            \
      {coinft_models::side##_w3, coinft_models::side##_b3, 36, 24},            \
      {coinft_models::side##_w4, coinft_models::side##_b4, 24, 12},            \
      {coinft_models::side##_w5, coinft_models::side##_b5, 12, 6}

const DenseLayer kLeftLayers[] = {COINFT_LAYERS(left)};
const DenseLayer kRightLayers[] = {COINFT_LAYERS(right)};

const Model kLeftModel = {"left",        coinft_models::left_mu_x,
                          coinft_models::left_sd_x, coinft_models::left_mu_y,
                          coinft_models::left_sd_y, kLeftLayers, 6};
const Model kRightModel = {"right",       coinft_models::right_mu_x,
                           coinft_models::right_sd_x, coinft_models::right_mu_y,
                           coinft_models::right_sd_y, kRightLayers, 6};

#undef COINFT_LAYERS

}  // namespace

const Model& leftModel() { return kLeftModel; }

const Model& rightModel() { return kRightModel; }

bool infer(const Model& model, const float raw[12], float wrench[6]) {
  float first[128];
  float second[128];
  float* input = first;
  float* output = second;

  for (unsigned int index = 0; index < 12; ++index) {
    if (!std::isfinite(raw[index]) || !std::isfinite(model.sd_x[index]) ||
        model.sd_x[index] == 0.0f) {
      return false;
    }
    input[index] = (raw[index] - model.mu_x[index]) / model.sd_x[index];
  }

  for (unsigned int layer_index = 0; layer_index < model.layer_count;
       ++layer_index) {
    const DenseLayer& layer = model.layers[layer_index];
    for (unsigned int neuron = 0; neuron < layer.output_size; ++neuron) {
      float sum = layer.bias[neuron];
      const float* weights = layer.weights + neuron * layer.input_size;
      for (unsigned int input_index = 0; input_index < layer.input_size;
           ++input_index) {
        sum += weights[input_index] * input[input_index];
      }
      if (layer_index + 1 < model.layer_count && sum < 0.0f) {
        sum = 0.0f;
      }
      output[neuron] = sum;
    }
    float* swap = input;
    input = output;
    output = swap;
  }

  for (unsigned int index = 0; index < 6; ++index) {
    wrench[index] = input[index] * model.sd_y[index] + model.mu_y[index];
    if (!std::isfinite(wrench[index])) {
      return false;
    }
  }
  return true;
}

}  // namespace coinft
