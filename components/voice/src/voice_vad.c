#include "voice_vad.h"
#include <math.h>

int voice_vad_energy(const int16_t *samples, size_t count)
{
    if (!samples || count == 0) return 0;

    int64_t sum_sq = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = (int32_t)samples[i];
        sum_sq += s * s;
    }

    /* Integer square root of mean squared value */
    int64_t mean_sq = sum_sq / (int64_t)count;
    if (mean_sq <= 0) return 0;

    /* Newton's method for integer sqrt */
    int64_t x = mean_sq;
    int64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + mean_sq / x) / 2;
    }
    return (int)x;
}

bool voice_vad_is_speech(const int16_t *samples, size_t count, int threshold)
{
    int energy = voice_vad_energy(samples, count);
    return energy > threshold;
}

int voice_vad_calibrate(const int16_t *samples, size_t count)
{
    int noise_floor = voice_vad_energy(samples, count);
    /* Set threshold at 3x the noise floor, minimum 200 */
    int threshold = noise_floor * 3;
    if (threshold < 200) threshold = 200;
    return threshold;
}
