// Pure-logic jitter/ring buffer for streamed PCM audio on StackChan
// (Path A2 playback path). Header-only and Arduino-free so the native test
// env can exercise it; the device uses one static instance fed by the UDP
// receiver and drained by the speaker pump in audio_play.cpp.
//
// Behaviour on overflow is OVERWRITE-OLDEST: live audio should never block
// the network receiver, and dropping the stalest samples keeps latency
// bounded when playback can't keep up. Loss shows up as a small click, which
// is the right trade for a real-time voice stream.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

template <size_t CAP>
class AudioRingBuffer {
   public:
    AudioRingBuffer() : head_(0), count_(0) {}

    size_t capacity() const { return CAP; }
    size_t available() const { return count_; }
    size_t freeSpace() const { return CAP - count_; }
    bool empty() const { return count_ == 0; }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

    // Append `len` bytes. If they don't fit, the oldest bytes are dropped to
    // make room (overwrite-oldest). If `len` exceeds CAP, only the final CAP
    // bytes of the input are retained. Always "succeeds"; returns the number
    // of input bytes stored (== min(len, CAP)).
    size_t push(const uint8_t* data, size_t len) {
        if (data == nullptr || len == 0) return 0;

        // Input larger than the whole buffer: keep only its tail.
        if (len >= CAP) {
            data += (len - CAP);
            len = CAP;
            head_ = 0;
            count_ = CAP;
            memcpy(buf_, data, CAP);
            return CAP;
        }

        // Drop oldest bytes if we'd overflow.
        if (len > freeSpace()) {
            size_t drop = len - freeSpace();
            head_ = (head_ + drop) % CAP;
            count_ -= drop;
        }

        size_t tail = (head_ + count_) % CAP;
        size_t first = CAP - tail;          // bytes until the physical end
        if (first > len) first = len;
        memcpy(buf_ + tail, data, first);
        if (first < len) {
            memcpy(buf_, data + first, len - first);  // wrap to front
        }
        count_ += len;
        return len;
    }

    // Copy up to `maxLen` bytes out (FIFO) into `out`, removing them.
    // Returns the number of bytes copied (min(available, maxLen)).
    size_t pop(uint8_t* out, size_t maxLen) {
        if (out == nullptr || maxLen == 0 || count_ == 0) return 0;
        size_t n = (maxLen < count_) ? maxLen : count_;

        size_t first = CAP - head_;         // bytes until the physical end
        if (first > n) first = n;
        memcpy(out, buf_ + head_, first);
        if (first < n) {
            memcpy(out + first, buf_, n - first);  // wrap to front
        }
        head_ = (head_ + n) % CAP;
        count_ -= n;
        return n;
    }

   private:
    uint8_t buf_[CAP];
    size_t head_;   // index of oldest byte
    size_t count_;  // bytes currently stored
};
