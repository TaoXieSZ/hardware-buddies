#include "adpcm.h"
#include <stdint.h>

// IMA ADPCM step size table (89 entries) — ITU-T G.726 / IMA reference.
static const int16_t step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

// Step index adjustment table indexed by nibble (0..7, mirrored for 8..15).
static const int8_t index_table[8] = {
    -1, -1, -1, -1, 2, 4, 6, 8
};

// Clamp helpers
static inline int16_t clamp16(int32_t v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static inline uint8_t clamp_index(int idx) {
    if (idx < 0)  return 0;
    if (idx > 88) return 88;
    return (uint8_t)idx;
}

// Encode one PCM sample → 4-bit nibble (0..15).
static uint8_t encode_sample(adpcm_state_t* s, int16_t sample) {
    int32_t step  = step_table[s->step_index];
    int32_t diff  = (int32_t)sample - (int32_t)s->predictor;
    uint8_t nibble = 0;

    if (diff < 0) { nibble = 8; diff = -diff; }

    if (diff >= step)           { nibble |= 4; diff -= step; }
    if (diff >= (step >> 1))    { nibble |= 2; diff -= (step >> 1); }
    if (diff >= (step >> 2))    { nibble |= 1; }

    // Update predictor
    int32_t delta = (step >> 3);
    if (nibble & 4) delta += step;
    if (nibble & 2) delta += (step >> 1);
    if (nibble & 1) delta += (step >> 2);
    if (nibble & 8) delta = -delta;

    s->predictor   = clamp16(s->predictor + delta);
    s->step_index  = clamp_index(s->step_index + index_table[nibble & 7]);

    return nibble;
}

// Decode one 4-bit nibble → PCM sample.
static int16_t decode_nibble(adpcm_state_t* s, uint8_t nibble) {
    int32_t step  = step_table[s->step_index];

    int32_t delta = (step >> 3);
    if (nibble & 4) delta += step;
    if (nibble & 2) delta += (step >> 1);
    if (nibble & 1) delta += (step >> 2);
    if (nibble & 8) delta = -delta;

    s->predictor   = clamp16(s->predictor + delta);
    s->step_index  = clamp_index(s->step_index + index_table[nibble & 7]);

    return s->predictor;
}

// ---- Public API ----

void adpcm_encode_init(adpcm_state_t* s) {
    s->predictor  = 0;
    s->step_index = 0;
}

size_t adpcm_encode(adpcm_state_t* s, const int16_t* in, size_t in_samples, uint8_t* out) {
    size_t bytes_written = 0;
    for (size_t i = 0; i + 1 < in_samples; i += 2) {
        uint8_t lo = encode_sample(s, in[i]);
        uint8_t hi = encode_sample(s, in[i + 1]);
        out[bytes_written++] = lo | (hi << 4);
    }
    return bytes_written;
}

void adpcm_decode_init(adpcm_state_t* s) {
    s->predictor  = 0;
    s->step_index = 0;
}

size_t adpcm_decode(adpcm_state_t* s, const uint8_t* in, size_t in_bytes, int16_t* out) {
    size_t samples_written = 0;
    for (size_t i = 0; i < in_bytes; i++) {
        uint8_t byte = in[i];
        out[samples_written++] = decode_nibble(s, byte & 0x0F);
        out[samples_written++] = decode_nibble(s, (byte >> 4) & 0x0F);
    }
    return samples_written;
}
