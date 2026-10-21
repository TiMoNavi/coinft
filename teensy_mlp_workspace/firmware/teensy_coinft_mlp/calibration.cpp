#include "calibration.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "inference.h"

namespace coinft {
namespace {

const uint32_t kRawStabilityWindowUs = 2000000;
const uint32_t kRawStabilityMinWaitUs = 5000000;
const uint32_t kFirstStartupStabilityMinWaitUs = 20000000;
const uint32_t kRawStabilityCheckIntervalUs = 500000;
const uint32_t kRuntimeSettlingUs = 5000000;
const uint32_t kFinalTareWindowUs = 2000000;
const uint32_t kQuickTareWindowUs = 1000000;
const uint32_t kBiasWindowUs = 1000000;
const uint32_t kZeroCheckWindowUs = 1000000;
const float kStableMeanDeltaCounts = 8.0f;
const float kStableNoiseStdCounts = 20.0f;
const uint8_t kRequiredStabilityPasses = 3;
const float kZeroForceLimitN = 0.15f;

}  // namespace

CoinFTCalibration::CoinFTCalibration()
    : history_next_(0),
      history_count_(0),
      status_(kWarmingUp),
      started_(false),
      started_at_us_(0),
      stability_check_started_(false),
      last_stability_check_us_(0),
      stability_passes_(0),
      settling_started_us_(0),
      quick_tare_(false),
      has_completed_calibration_(false),
      require_boot_stability_(false),
      bias_started_us_(0),
      zero_started_us_(0),
      zero_check_warning_(false),
      bias_count_(0),
      filter_next_(0),
      filter_count_(0),
      zero_count_(0) {
  memset(raw_offset_, 0, sizeof(raw_offset_));
  memset(ft_bias_, 0, sizeof(ft_bias_));
  memset(bias_sum_, 0, sizeof(bias_sum_));
  memset(filter_history_, 0, sizeof(filter_history_));
  memset(zero_force_sum_, 0, sizeof(zero_force_sum_));
}

void CoinFTCalibration::reset() {
  history_next_ = 0;
  history_count_ = 0;
  status_ = kWarmingUp;
  started_ = false;
  started_at_us_ = 0;
  stability_check_started_ = false;
  last_stability_check_us_ = 0;
  stability_passes_ = 0;
  settling_started_us_ = 0;
  quick_tare_ = false;
  has_completed_calibration_ = false;
  require_boot_stability_ = false;
  bias_started_us_ = 0;
  zero_started_us_ = 0;
  zero_check_warning_ = false;
  bias_count_ = 0;
  filter_next_ = 0;
  filter_count_ = 0;
  zero_count_ = 0;
  memset(raw_offset_, 0, sizeof(raw_offset_));
  memset(ft_bias_, 0, sizeof(ft_bias_));
  memset(bias_sum_, 0, sizeof(bias_sum_));
  memset(filter_history_, 0, sizeof(filter_history_));
  memset(zero_force_sum_, 0, sizeof(zero_force_sum_));
}

void CoinFTCalibration::begin() {
  const bool use_quick_tare = has_completed_calibration_;
  reset();
  if (!use_quick_tare) {
    require_boot_stability_ = true;
    return;
  }

  // The sensor streams have just been restarted. Capture one unloaded raw
  // second, then recompute the model bias and run the normal zero check.
  has_completed_calibration_ = true;
  quick_tare_ = true;
  status_ = kRuntimeSettling;
}

void CoinFTCalibration::appendRaw(const uint16_t left_raw[12],
                                  const uint16_t right_raw[12],
                                  uint32_t timestamp_us) {
  RawSample& sample = history_[history_next_];
  sample.timestamp_us = timestamp_us;
  memcpy(sample.left, left_raw, sizeof(sample.left));
  memcpy(sample.right, right_raw, sizeof(sample.right));
  history_next_ = (history_next_ + 1) % kHistoryCapacity;
  if (history_count_ < kHistoryCapacity) ++history_count_;
}

void CoinFTCalibration::clearRawHistory() {
  history_next_ = 0;
  history_count_ = 0;
}

bool CoinFTCalibration::windowStats(uint32_t now_us, uint32_t min_age_us,
                                    uint32_t max_age_us, float means[2][12],
                                    float standard_deviations[24]) const {
  double sums[2][12] = {};
  double sums_squared[2][12] = {};
  uint32_t count = 0;
  uint32_t first_timestamp = 0;
  uint32_t last_timestamp = 0;
  const size_t oldest = (history_next_ + kHistoryCapacity - history_count_) % kHistoryCapacity;

  for (size_t index = 0; index < history_count_; ++index) {
    const RawSample& sample = history_[(oldest + index) % kHistoryCapacity];
    const uint32_t age_us = now_us - sample.timestamp_us;
    if (age_us < min_age_us || age_us > max_age_us) continue;
    if (count == 0) first_timestamp = sample.timestamp_us;
    last_timestamp = sample.timestamp_us;
    ++count;
    for (size_t channel = 0; channel < 12; ++channel) {
      const double left = sample.left[channel];
      const double right = sample.right[channel];
      sums[0][channel] += left;
      sums[1][channel] += right;
      sums_squared[0][channel] += left * left;
      sums_squared[1][channel] += right * right;
    }
  }

  if (count < 2 || static_cast<uint32_t>(last_timestamp - first_timestamp) <
                       static_cast<uint32_t>((max_age_us - min_age_us) * 0.7f)) {
    return false;
  }

  float deviations[24];
  for (size_t side = 0; side < 2; ++side) {
    for (size_t channel = 0; channel < 12; ++channel) {
      const double mean = sums[side][channel] / count;
      const double variance = std::max(0.0, sums_squared[side][channel] / count - mean * mean);
      means[side][channel] = static_cast<float>(mean);
      deviations[side * 12 + channel] = static_cast<float>(std::sqrt(variance));
    }
  }
  if (standard_deviations != NULL) {
    memcpy(standard_deviations, deviations, sizeof(deviations));
  }
  return true;
}

void CoinFTCalibration::advanceStability(uint32_t timestamp_us) {
  const uint32_t minimum_wait_us = require_boot_stability_
                                       ? kFirstStartupStabilityMinWaitUs
                                       : kRawStabilityMinWaitUs;
  if (static_cast<uint32_t>(timestamp_us - started_at_us_) < minimum_wait_us) {
    status_ = kWarmingUp;
    return;
  }
  if (stability_check_started_ &&
      static_cast<uint32_t>(timestamp_us - last_stability_check_us_) <
          kRawStabilityCheckIntervalUs) {
    return;
  }
  stability_check_started_ = true;
  last_stability_check_us_ = timestamp_us;

  float previous_means[2][12];
  float current_means[2][12];
  float deviations[24];
  if (!windowStats(timestamp_us, kRawStabilityWindowUs,
                   2 * kRawStabilityWindowUs,
                   previous_means, NULL) ||
      !windowStats(timestamp_us, 0, kRawStabilityWindowUs, current_means,
                   deviations)) {
    status_ = kDelayed;
    stability_passes_ = 0;
    return;
  }

  float max_mean_delta = 0.0f;
  for (size_t side = 0; side < 2; ++side) {
    for (size_t channel = 0; channel < 12; ++channel) {
      max_mean_delta = std::max(
          max_mean_delta,
          std::fabs(current_means[side][channel] - previous_means[side][channel]));
    }
  }
  std::sort(deviations, deviations + 24);
  const float median_noise = (deviations[11] + deviations[12]) * 0.5f;
  const bool stable = max_mean_delta <= kStableMeanDeltaCounts &&
                      median_noise <= kStableNoiseStdCounts;
  stability_passes_ = stable ? static_cast<uint8_t>(stability_passes_ + 1) : 0;
  if (stability_passes_ < kRequiredStabilityPasses) {
    status_ = kDelayed;
    return;
  }

  memcpy(raw_offset_, current_means, sizeof(raw_offset_));
  resetFilter();
  status_ = kProvisionalTare;
}

void CoinFTCalibration::advanceFinalTare(uint32_t timestamp_us) {
  if (static_cast<uint32_t>(timestamp_us - settling_started_us_) <
      kRuntimeSettlingUs) {
    return;
  }
  float means[2][12];
  if (!windowStats(timestamp_us, 0, kFinalTareWindowUs, means, NULL)) return;
  memcpy(raw_offset_, means, sizeof(raw_offset_));
  resetFilter();
  clearRawHistory();
  status_ = kFinalTare;
}

void CoinFTCalibration::advanceQuickTare(uint32_t timestamp_us) {
  if (static_cast<uint32_t>(timestamp_us - started_at_us_) < kQuickTareWindowUs) {
    return;
  }
  float means[2][12];
  if (!windowStats(timestamp_us, 0, kQuickTareWindowUs, means, NULL)) return;
  memcpy(raw_offset_, means, sizeof(raw_offset_));
  resetFilter();
  clearRawHistory();
  bias_started_us_ = timestamp_us;
  bias_count_ = 0;
  memset(bias_sum_, 0, sizeof(bias_sum_));
  status_ = kBiasCalibration;
}

bool CoinFTCalibration::predictUncorrected(const uint16_t raw[12],
                                           unsigned int side,
                                           float wrench[6]) const {
  float centered[12];
  for (size_t channel = 0; channel < 12; ++channel) {
    centered[channel] = static_cast<float>(raw[channel]) - raw_offset_[side][channel];
  }
  return infer(side == 0 ? leftModel() : rightModel(), centered, wrench);
}

bool CoinFTCalibration::predictCorrected(const uint16_t left_raw[12],
                                         const uint16_t right_raw[12],
                                         float left_wrench[6],
                                         float right_wrench[6]) {
  float left_uncorrected[6];
  float right_uncorrected[6];
  if (!predictUncorrected(left_raw, 0, left_uncorrected) ||
      !predictUncorrected(right_raw, 1, right_uncorrected)) {
    return false;
  }
  for (size_t axis = 0; axis < 6; ++axis) {
    filter_history_[0][filter_next_][axis] = left_uncorrected[axis] - ft_bias_[0][axis];
    filter_history_[1][filter_next_][axis] = right_uncorrected[axis] - ft_bias_[1][axis];
  }
  filter_next_ = (filter_next_ + 1) % kFilterWindow;
  if (filter_count_ < kFilterWindow) ++filter_count_;

  for (size_t axis = 0; axis < 6; ++axis) {
    double left_sum = 0.0;
    double right_sum = 0.0;
    for (size_t index = 0; index < filter_count_; ++index) {
      left_sum += filter_history_[0][index][axis];
      right_sum += filter_history_[1][index][axis];
    }
    left_wrench[axis] = static_cast<float>(left_sum / filter_count_);
    right_wrench[axis] = static_cast<float>(right_sum / filter_count_);
    if (!std::isfinite(left_wrench[axis]) || !std::isfinite(right_wrench[axis])) {
      return false;
    }
  }
  return true;
}

void CoinFTCalibration::resetFilter() {
  filter_next_ = 0;
  filter_count_ = 0;
}

void CoinFTCalibration::fillOutput(CalibrationOutput* output, bool has_wrench,
                                   const float left[6],
                                   const float right[6]) const {
  output->status = status_;
  output->has_wrench = has_wrench;
  output->zero_check_warning = zero_check_warning_;
  for (size_t axis = 0; axis < 6; ++axis) {
    output->left_wrench[axis] = has_wrench ? left[axis] : 0.0f;
    output->right_wrench[axis] = has_wrench ? right[axis] : 0.0f;
  }
}

void CoinFTCalibration::process(const uint16_t left_raw[12],
                                const uint16_t right_raw[12],
                                uint32_t timestamp_us,
                                CalibrationOutput* output) {
  appendRaw(left_raw, right_raw, timestamp_us);
  if (!started_) {
    started_ = true;
    started_at_us_ = timestamp_us;
  }

  if (status_ == kWarmingUp || status_ == kDelayed) {
    advanceStability(timestamp_us);
    if (status_ == kWarmingUp || status_ == kDelayed) {
      fillOutput(output, false, NULL, NULL);
      return;
    }
  }

  if (status_ == kProvisionalTare) {
    status_ = kRuntimeSettling;
    settling_started_us_ = timestamp_us;
    fillOutput(output, false, NULL, NULL);
    return;
  }

  if (status_ == kRuntimeSettling) {
    if (quick_tare_) {
      advanceQuickTare(timestamp_us);
    } else {
      advanceFinalTare(timestamp_us);
    }
    fillOutput(output, false, NULL, NULL);
    return;
  }

  if (status_ == kFinalTare) {
    status_ = kBiasCalibration;
    bias_started_us_ = timestamp_us;
    bias_count_ = 0;
    memset(bias_sum_, 0, sizeof(bias_sum_));
  }

  if (status_ == kBiasCalibration) {
    float left[6];
    float right[6];
    if (!predictUncorrected(left_raw, 0, left) ||
        !predictUncorrected(right_raw, 1, right)) {
      status_ = kCalibrationError;
      fillOutput(output, false, NULL, NULL);
      return;
    }
    for (size_t axis = 0; axis < 6; ++axis) {
      bias_sum_[0][axis] += left[axis];
      bias_sum_[1][axis] += right[axis];
    }
    ++bias_count_;
    if (static_cast<uint32_t>(timestamp_us - bias_started_us_) < kBiasWindowUs) {
      fillOutput(output, false, NULL, NULL);
      return;
    }
    for (size_t side = 0; side < 2; ++side) {
      for (size_t axis = 0; axis < 6; ++axis) {
        ft_bias_[side][axis] = static_cast<float>(bias_sum_[side][axis] / bias_count_);
      }
    }
    resetFilter();
    memset(zero_force_sum_, 0, sizeof(zero_force_sum_));
    zero_count_ = 0;
    zero_started_us_ = timestamp_us;
    status_ = kZeroCheck;
    fillOutput(output, false, NULL, NULL);
    return;
  }

  if (status_ == kZeroCheckFailed || status_ == kCalibrationError) {
    fillOutput(output, false, NULL, NULL);
    return;
  }

  float left[6];
  float right[6];
  if (!predictCorrected(left_raw, right_raw, left, right)) {
    status_ = kCalibrationError;
    fillOutput(output, false, NULL, NULL);
    return;
  }
  if (status_ == kZeroCheck) {
    for (size_t axis = 0; axis < 3; ++axis) {
      zero_force_sum_[0][axis] += left[axis];
      zero_force_sum_[1][axis] += right[axis];
    }
    ++zero_count_;
    if (static_cast<uint32_t>(timestamp_us - zero_started_us_) >=
        kZeroCheckWindowUs) {
      float max_abs_force = 0.0f;
      for (size_t side = 0; side < 2; ++side) {
        for (size_t axis = 0; axis < 3; ++axis) {
          const float mean = static_cast<float>(zero_force_sum_[side][axis] / zero_count_);
          max_abs_force = std::max(max_abs_force, std::fabs(mean));
        }
      }
      zero_check_warning_ = max_abs_force > kZeroForceLimitN;
      status_ = kReady;
      has_completed_calibration_ = true;
    }
  }
  fillOutput(output, status_ == kReady, left, right);
}

}  // namespace coinft
