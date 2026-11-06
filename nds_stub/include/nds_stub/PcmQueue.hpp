#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace beiklive::nds_stub {
// Caller serialises producer, reader and timeline changes with the audio lock.
class PcmQueue {
public:
    void setNormalSpeed(bool enabled) {
        if (normalSpeed == enabled) return;
        normalSpeed = enabled;
        samples.resize((enabled ? 32823u * 30 : 4096u) * 2);
        clear();
    }
    void clear() { head = count = 0; }
    void push(const std::int16_t *src, std::size_t frames) {
        const auto capacity = samples.size() / 2;
        // Preserve the normal-speed timeline, up to the same 30 s bound as
        // the donor. Once full, discard new blocks instead of speeding up.
        if (normalSpeed && frames > capacity - count) return;
        if (frames > capacity) { src += (frames - capacity) * 2; frames = capacity; }
        if (count + frames > capacity) {
            const auto drop = count + frames - capacity;
            head = (head + drop) % capacity;
            count -= drop;
        }
        const auto tail = (head + count) % capacity;
        const auto first = std::min(frames, capacity - tail);
        std::copy_n(src, first * 2, samples.data() + tail * 2);
        std::copy_n(src + first * 2, (frames - first) * 2, samples.data());
        count += frames;
    }
    int read(std::int16_t *dst, int wanted) {
        if (wanted <= 0) return 0;
        const auto frames = std::min<std::size_t>(wanted, count);
        const auto capacity = samples.size() / 2;
        const auto first = std::min(frames, capacity - head);
        std::copy_n(samples.data() + head * 2, first * 2, dst);
        std::copy_n(samples.data(), (frames - first) * 2, dst + first * 2);
        head = (head + frames) % capacity;
        count -= frames;
        return static_cast<int>(frames);
    }
    std::size_t size() const { return count; }
private:
    std::vector<std::int16_t> samples = std::vector<std::int16_t>(4096 * 2);
    std::size_t head = 0, count = 0;
    bool normalSpeed = false;
};
}
