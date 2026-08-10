#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstddef>

// InterpDelay
// -----------
// A circular buffer with linear-interpolated read tap plus multi-tap read.
// The Dattorro reverb instantiates many of these; their combined size at
// maxTimeScale=4 on 48 kHz exceeds the Daisy Seed SRAM heap. To let the
// caller place the storage in SDRAM (or any other memory region), an optional
// external "arena" can be installed. If set, InterpDelay carves its buffer
// from that arena (bump-allocated, non-freeing) instead of using the heap.
// If no arena is set the class falls back to std::vector<float>, matching
// the original behavior; this keeps the class usable in desktop tests.

class InterpDelayArena {
public:
    // Install an external arena. All InterpDelay instances constructed after
    // this call will bump-allocate from `base` up to `size` floats.
    // Passing base=nullptr clears the arena (subsequent instances fall back
    // to heap).
    static void set(float* base, size_t size) {
        base_ = base;
        capacity_ = size;
        used_ = 0;
        exhausted_ = false;
    }
    static float* allocate(size_t n) {
        if (!base_ || used_ + n > capacity_) {
            exhausted_ = (base_ != nullptr);
            return nullptr;
        }
        float* p = base_ + used_;
        used_ += n;
        return p;
    }
    static size_t used() { return used_; }
    static size_t capacity() { return capacity_; }
    static bool exhausted() { return exhausted_; }

private:
    inline static float* base_ = nullptr;
    inline static size_t capacity_ = 0;
    inline static size_t used_ = 0;
    inline static bool exhausted_ = false;
};

class InterpDelay {
public:
    float input = 0.;
    float output = 0.;

    InterpDelay(unsigned int maxLength = 512, float initDelayTime = 0.) {
        l = maxLength;
        lfloat = static_cast<float>(maxLength);

        // Try the arena first; fall back to owning storage if not set.
        buffer = InterpDelayArena::allocate(maxLength);
        if (buffer == nullptr) {
            owned.assign(maxLength, 0.0f);
            buffer = owned.data();
        } else {
            std::memset(buffer, 0, sizeof(float) * maxLength);
        }

        setDelayTime(initDelayTime);
    }

    // Move constructor / move assignment: if we own storage, transfer it;
    // if the buffer points into an arena, just re-point.
    InterpDelay(InterpDelay&& other) noexcept { *this = std::move(other); }
    InterpDelay& operator=(InterpDelay&& other) noexcept {
        if (this != &other) {
            input = other.input;
            output = other.output;
            owned = std::move(other.owned);
            // If other used owned storage, our buffer must point into our own now.
            if (other.buffer == other.owned.data() && !owned.empty()) {
                buffer = owned.data();
            } else {
                buffer = other.buffer;
            }
            w = other.w; r = other.r; upperR = other.upperR;
            j = other.j; t = other.t; f = other.f;
            l = other.l; lfloat = other.lfloat;
            dataR = other.dataR; dataUpperR = other.dataUpperR;
            other.buffer = nullptr;
        }
        return *this;
    }

    // Non-copyable to avoid accidental aliasing of external buffers.
    InterpDelay(const InterpDelay&) = delete;
    InterpDelay& operator=(const InterpDelay&) = delete;

    #pragma GCC push_options
    #pragma GCC optimize ("Ofast")

    inline void process() {
        buffer[w] = input;
        r = w - t;

        if (r < 0) {
            r += l;
        }

        ++w;
        if (w >= l) {
            w = 0;
        }

        upperR = r - 1;
        if (upperR < 0) {
            upperR += l;
        }

        dataR = buffer[r];
        dataUpperR = buffer[upperR];

        output = dataR + f * (dataUpperR - dataR);
    }

    #pragma GCC pop_options

    #pragma GCC push_options
    #pragma GCC optimize ("Ofast")

    inline float tap(const int &i) {
        j = w - i;
        if (j < 0) {
            j += l;
        }
        return buffer[j];
    }

    #pragma GCC pop_options

    #pragma GCC push_options
    #pragma GCC optimize ("Ofast")

    inline void setDelayTime(float newDelayTime) {
        if (newDelayTime >= lfloat) {
            newDelayTime = lfloat - 1.;
        }
        if (newDelayTime < 0.) {
            newDelayTime = 0.;
        }
        t = static_cast<int>(newDelayTime);
        f = newDelayTime - static_cast<float>(t);
    }

    #pragma GCC pop_options

    void clear() {
        if (buffer) {
            std::memset(buffer, 0, sizeof(float) * l);
        }
        input = 0.;
        output = 0.;
    }

private:
    float* buffer = nullptr;      // Points into arena or into `owned`.
    std::vector<float> owned;     // Fallback storage when no arena is set.
    int w = 0;
    int r = 0;
    int upperR = 0;
    int j = 0;
    int t = 0;
    float f = 0.;
    int l = 512;
    float lfloat = 512.;
    float dataR = 0.;
    float dataUpperR = 0.;
};
