#pragma once

#include "audio_constants.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace stopwatch {

struct PcmChunk {
    std::array<int16_t, kPcmSamplesPerChunk> samples{};
    size_t sample_count = 0;
};

class AudioQueue {
public:
    bool push(const int16_t* samples, size_t sample_count)
    {
        if (sample_count > kPcmSamplesPerChunk || count_ >= chunks_.size()) {
            overflowed_ = true;
            return false;
        }
        auto& chunk = chunks_[tail_];
        if (sample_count > 0) {
            std::memcpy(chunk.samples.data(), samples, sample_count * sizeof(int16_t));
        }
        chunk.sample_count = sample_count;
        tail_ = (tail_ + 1) % chunks_.size();
        ++count_;
        if (count_ > high_water_) {
            high_water_ = count_;
        }
        return true;
    }

    bool pop(PcmChunk& out)
    {
        if (count_ == 0) {
            return false;
        }
        out = chunks_[head_];
        head_ = (head_ + 1) % chunks_.size();
        --count_;
        return true;
    }

    void clear()
    {
        head_ = 0;
        tail_ = 0;
        count_ = 0;
        overflowed_ = false;
    }

    size_t size() const { return count_; }
    size_t capacity() const { return chunks_.size(); }
    size_t high_water() const { return high_water_; }
    bool overflowed() const { return overflowed_; }

private:
    std::array<PcmChunk, kAudioQueueCapacity> chunks_{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    size_t high_water_ = 0;
    bool overflowed_ = false;
};

}  // namespace stopwatch
