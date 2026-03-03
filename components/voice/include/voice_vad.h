#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Simple energy-based Voice Activity Detection.
 * Computes RMS energy of a frame and compares against threshold.
 */

/**
 * Compute RMS energy of a PCM frame.
 * @param samples  Signed 16-bit PCM samples
 * @param count    Number of samples
 * @return RMS energy value (0-32767)
 */
int voice_vad_energy(const int16_t *samples, size_t count);

/**
 * Check if a frame contains voice activity.
 * @param samples    PCM samples
 * @param count      Number of samples
 * @param threshold  Energy threshold (typical: 200-1000)
 * @return true if voice activity detected
 */
bool voice_vad_is_speech(const int16_t *samples, size_t count, int threshold);

/**
 * Auto-calibrate noise floor.
 * Call with a few frames of ambient noise to determine threshold.
 * @param samples    PCM samples of ambient noise
 * @param count      Number of samples
 * @return Suggested threshold (noise_floor * 3)
 */
int voice_vad_calibrate(const int16_t *samples, size_t count);
