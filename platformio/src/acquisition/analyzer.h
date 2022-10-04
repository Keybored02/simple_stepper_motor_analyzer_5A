// API of the data acquisition and step decoder modole. This
// is the core of the analysis software and it is executed
// in an interrupt routine so handle with care.

#pragma once

#include <string.h>

#include "misc/circular_buffer.h"

namespace analyzer {

// This is set in the Settings page.
struct Settings {
  // Offsets to substract from the ADC readings to have zero reading
  // when the current is zero.
  // Typically around ~1900 which represents ~1.5V from the current
  // sensors.
  int16_t offset1;
  int16_t offset2;
  // If true, reverse interpretation of forward/backward movement.
  bool reverse_direction;
};

// Number of pairs of ADC readings to capture for the signal
// capture page. The capture logic try to sync a ch1 up crossing
// the horizontal axis at the middle of the buffer for better
// visual stability.
//
// NOTE: 400 is the width in pixels of the active plot area of the
// osciloscope screen.
constexpr uint32_t kAdcCaptureBufferSize = 400;

// A single captured item. These are the signed values
// in adc counts of the two curent sensing channels.
struct AdcCaptureItem {
  int16_t v1;
  int16_t v2;
};

// A circular array with captured signals. Using a circular array
// allow to capture data before the trigger point.
typedef CircularBuffer<AdcCaptureItem, kAdcCaptureBufferSize> AdcCaptureItems;

struct AdcCaptureBuffer {
  AdcCaptureItems items;
  // True if capture was synced with a trigger event where
  // v1 crossed up the y=0 axis. The triggered event, if available
  // is always at a fixed index at the middle of the returned
  // captured range.
  bool trigger_found;
};

// Max number of capture steps items. Stpes are captures at
// a slow rate so a small number is suffient for the
// UI to catch up considering the worst case screen update
// time.
constexpr uint32_t kStepsCaptureBufferSize = 10;

constexpr uint32_t kStepsCaptursPerSec = 20;

struct StepsCaptureItem {
  // Snapshots of the corresponding fields in State object.
  int full_steps;
  int max_full_steps;
};

typedef CircularBuffer<StepsCaptureItem, kStepsCaptureBufferSize> StepsCaptureBuffer;

// Step direction classification. The analyzer classifies
// each step with these gats. Unknown happens when direction
// is reversed at the middle of the step.
enum Direction { UNKNOWN_DIRECTION, FORWARD, BACKWARD };

// A sampling tick includes a pair of ADC sampling one from
// each of the two channels.
// Sampling rate is 100K sampling-pairs/sec. Should match ADC
// settings (which samples at a 2x rate, alternating betwee
// the two channels)
constexpr int kUsecsPerTick = 10;
constexpr int TicksPerSecond = 1000000 / kUsecsPerTick;

// Max range when using ACS70331EOLCTR-2P5B3 (+/- 2.5A).
// Double this if using the +/-5A current sensor variant.
//constexpr int kMaxMilliamps = 2500;

// Number of histogram buckets, each bucket represents
// a band of step speeds.
constexpr int kNumHistogramBuckets = 20;

// Each histogram bucket represents a speed range of 100
// steps/sec, starting from zero. Overflow speeds are
// aggregated in the last bucket.
const int kBucketStepsPerSecond = 100;

// A single histogram bucket
struct HistogramBucket {
  // Total adc samples in steps in this bucket. This is a proxy
  // for time spent in this speed range.
  uint64_t total_ticks_in_steps;
  // Total max step current in ADC counts. Used
  // to compute the average max coil curent by speed range.
  uint64_t total_step_peak_currents;
  // Total steps. This is a proxy for the distance (in either direction)
  // done in this speed range.
  uint32_t total_steps;
};

// Analyzer data, other than the capture buffers, this is the only
// data that the interrupt routine updates.
struct State {
 public:
  State()
      : tick_count(0),
        ticks_with_errors(0),
        v1(0),
        v2(0),
        is_energized(false),
        non_energized_count(0),
        quadrant(0),
        full_steps(0),
        max_full_steps(0),
        max_retraction_steps(0),
        quadrature_errors(0),
        last_step_direction(UNKNOWN_DIRECTION),
        max_current_in_step(0),
        ticks_in_step(0) {
    memset(buckets, 0, sizeof(buckets));
  }

  // Number of ADC pair samples since last data reset. This is
  // also a proxy for the time passed.
  uint32_t tick_count;

  // Ticks that have the ADC error flag set.
  uint32_t ticks_with_errors;

  // Signed current values in ADC count units.  When the stepper
  // is energized, these values together with the quadrant value
  // below can be used to compute the fractional step value.
  int16_t v1;
  int16_t v2;
  // True if the coils are energized. Determined by the sum
  // of the absolute values of a pair of current readings.
  bool is_energized;
  // Number of times coils were de-energized.
  uint32_t non_energized_count;
  // The last quadrant in the range [0, 3]. Each quadrant
  // represents a full step. See quadrants_plot.png  for details.
  int8_t quadrant;
  // Total (forward - backward) full steps. This is a proxy
  // for the overall distance.
  int full_steps;
  // Max value of full_steps so far. Momentary retraction value
  // can computed as max(0, max_full_steps - full_steps).
  int max_full_steps;
  // Max value of (max_full_steps - full_steps). As of Jan 2021,
  // this value is computed but not used.
  int max_retraction_steps;
  // Total invalid quadrant transitions. Typically indicate
  // distorted stepper coils current patterns.
  uint32_t quadrature_errors;
  // Direction of last step. Used to track step speed.
  Direction last_step_direction;
  // Max current detected in the current step. We use a single non signed
  // value for both channels. in ADC count units.
  uint32_t max_current_in_step;
  // Time in current state, in 100Khz ADC sample time unit. This is
  // a proxy for the time in current step.
  uint32_t ticks_in_step;
  // Histogram, each bucket represents a range of steps/sec speeds.
  HistogramBucket buckets[kNumHistogramBuckets];
};

// Helpers for dumping aquisition sate. For debugging.
void dump_sampled_state();
void dump_adc_capture(const AdcCaptureBuffer& adc_capture_buffer);

// Called once during program initialization.
void setup(const Settings& settings);

// Is the capture buffer full with data?
bool is_adc_capture_ready();

// Call after is_capture_ready() is true, to get a pointer
// to internal buffer that contains the capture buffer.
const AdcCaptureBuffer* adc_capture_buffer();

// Start signal capturing. Data is ready when
// is_capture_ready() is true. If divider > 1,
// only one every n ADC samples is captured.
void start_adc_capture(uint16_t divider);

// Sample capture steps items since last call to this function.
// Returns a pointer to an internal buffer with the consumed 
// items, if any.
const StepsCaptureBuffer* sample_steps_capture();

// Sample the current state to an internal buffer and return
// a const ptr to it. Values are stable until next time
// this method is called.
const State* sample_state();

// Clears state data. This resets counters, min/max values,
// histograms, etc.
void reset_state();

// Return the steps value of the given state.
double state_steps(const State& state);

// Convert adc value to milliamps.
//int adc_value_to_milliamps(int adc_value);

// Convert adc value to amps.
//float adc_value_to_amps(int adc_value);

// Call this when the coil current is known to be zero to
// calibrate the internal offset1 and offset2.
void calibrate_zeros();

// Set direction. This updates the current settings.
// Controlled by the user in the Settings screen.
void set_direction(bool reverse_direction);

// Return a copy of the internal settings. Used after
// calibrate_zeros() to save the current settings in the
// EEPROM.
void get_settings(Settings* settings);

// @@@ Temp
void dump_dma_state();


// Variables for calculating rms datas from stored adc
// This one is always storing adc values
constexpr uint32_t kAdcRMSCaptureBufferSize = kAdcCaptureBufferSize;
typedef CircularBuffer<AdcCaptureItem, kAdcRMSCaptureBufferSize> AdcCaptureItemsforRMS;
struct AdcCaptureBufferforRMS {
  public:
    AdcCaptureBufferforRMS():
      counter(0),
      read_accept_flag(true),
      write_stop_flag(false)
      {

      }
  
  AdcCaptureItemsforRMS items;
  uint16_t counter;
  bool read_accept_flag;
  bool write_stop_flag;
};

void calc_true_rms();
AdcCaptureItem* get_rms_val();

}  // namespace analyzer
