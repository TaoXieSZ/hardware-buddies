#pragma once
#include <stddef.h>
#include <stdint.h>

// IMA (Intel/DVI) ADPCM codec — 4 bits/sample, 4:1 compression from 16-bit PCM.
// Reference: ITU-T G.726 / IMA ADPCM specification.

typedef struct {
    int16_t  predictor;   // running sample predictor
    uint8_t  step_index;  // index into step_table[]
} adpcm_state_t;

// Encoder API
void   adpcm_encode_init(adpcm_state_t* s);

// Encode `in_samples` int16 PCM samples into ADPCM nibbles packed 2-per-byte.
// in_samples MUST be even. Returns bytes written = in_samples / 2.
size_t adpcm_encode(adpcm_state_t* s, const int16_t* in, size_t in_samples, uint8_t* out);

// Decoder API
void   adpcm_decode_init(adpcm_state_t* s);

// Decode `in_bytes` packed-nibble ADPCM bytes into int16 PCM samples.
// Writes in_bytes * 2 samples into `out`. Returns samples written.
size_t adpcm_decode(adpcm_state_t* s, const uint8_t* in, size_t in_bytes, int16_t* out);
