#pragma once

#include <cstddef>
#include <cstdint>

namespace stopwatch {

constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kPcmSampleRateHz = 16000;
constexpr uint8_t kPcmBitsPerSample = 16;
constexpr uint8_t kPcmChannels = 1;
constexpr size_t kPcmChunkMs = 20;
constexpr size_t kPcmSamplesPerChunk = kPcmSampleRateHz * kPcmChunkMs / 1000;
constexpr size_t kPcmBytesPerChunk = kPcmSamplesPerChunk * sizeof(int16_t);
constexpr size_t kAudioQueueCapacity = 24;
constexpr uint32_t kReconnectInitialMs = 500;
constexpr uint32_t kReconnectMaxMs = 8000;
constexpr uint16_t kReleaseAckMs = 45;
constexpr uint8_t kReleaseAckStrength = 60;

}  // namespace stopwatch
