#pragma once

#include <stddef.h>
#include <stdint.h>

namespace coinft {

enum CalibrationStatus : uint8_t {
  kWarmingUp = 0,
  kDelayed = 1,
  kProvisionalTare = 2,
  kRuntimeSettling = 3,
  kFinalTare = 4,
  kBiasCalibration = 5,
  kZeroCheck = 6,
  kReady = 7,
  kZeroCheckFailed = 8,
  kCalibrationError = 9,
};

struct CalibrationOutput {
  CalibrationStatus status;
  bool has_wrench;
  bool zero_check_warning;
  float left_wrench[6];
  float right_wrench[6];
};

class CoinFTCalibration {
 public:
  CoinFTCalibration();
  // Start a new acquisition calibration. The first start after power-up runs
  // the complete stability/tare sequence; later starts use the short tare.
  void begin();
  void reset();

  void process(const uint16_t left_raw[12], const uint16_t right_raw[12],
               uint32_t timestamp_us, CalibrationOutput* output);

 private:
  struct RawSample {
    uint32_t timestamp_us;
    uint16_t left[12];
    uint16_t right[12];
  };

  static const size_t kHistoryCapacity = 4096;
  static const size_t kFilterWindow = 5;

  void appendRaw(const uint16_t left_raw[12], const uint16_t right_raw[12],
                 uint32_t timestamp_us);
  void clearRawHistory();
  bool windowStats(uint32_t now_us, uint32_t min_age_us,
                   uint32_t max_age_us, float means[2][12],
                   float standard_deviations[24]) const;
  void advanceStability(uint32_t timestamp_us);
  void advanceFinalTare(uint32_t timestamp_us);
  void advanceQuickTare(uint32_t timestamp_us);
  bool predictUncorrected(const uint16_t raw[12], unsigned int side,
                          float wrench[6]) const;
  bool predictCorrected(const uint16_t left_raw[12],
                        const uint16_t right_raw[12], float left_wrench[6],
                        float right_wrench[6]);
  void resetFilter();
  void fillOutput(CalibrationOutput* output, bool has_wrench,
                  const float left[6], const float right[6]) const;

  RawSample history_[kHistoryCapacity];
  size_t history_next_;
  size_t history_count_;
  CalibrationStatus status_;
  bool started_;
  uint32_t started_at_us_;
  bool stability_check_started_;
  uint32_t last_stability_check_us_;
  uint8_t stability_passes_;
  uint32_t settling_started_us_;
  bool quick_tare_;
  bool has_completed_calibration_;
  bool require_boot_stability_;
  uint32_t bias_started_us_;
  uint32_t zero_started_us_;
  bool zero_check_warning_;
  float raw_offset_[2][12];
  float ft_bias_[2][6];
  double bias_sum_[2][6];
  uint32_t bias_count_;
  float filter_history_[2][kFilterWindow][6];
  size_t filter_next_;
  size_t filter_count_;
  double zero_force_sum_[2][3];
  uint32_t zero_count_;
};

}  // namespace coinft
