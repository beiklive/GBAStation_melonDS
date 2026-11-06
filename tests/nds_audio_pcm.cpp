// c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined tests/nds_audio_pcm.cpp -o /tmp/nds_audio_pcm && /tmp/nds_audio_pcm
#include "../nds_stub/include/nds_stub/PcmBlock.hpp"
#include "../nds_stub/include/nds_stub/PcmQueue.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    constexpr int rate = 32823, frames = rate * 2, block = 768;
    std::vector<std::int16_t> tone(frames * 2);
    for (int i = 0; i < frames; ++i) {
        tone[2 * i] = std::lround(12000 * std::sin(2 * 3.141592653589793 * 1000 * i / rate));
        tone[2 * i + 1] = -tone[2 * i];
    }
    // Produce NDS-sized fragments, including partial reads, empty polls and
    // fast-forward batches. The delivered waveform must keep every sample
    // exactly once, with no held-sample tails and no stereo channel changes.
    for (const int speed : {1, 2, 5}) {
        int produced = 0, consumed = 0, batch = 0, shorts = 0;
        std::vector<std::int16_t> played;
        while (consumed < frames) {
            produced = std::min(frames, produced + speed * (548 + batch++ % 2));
            int usedSlots = 0;
            while (consumed < produced) {
                std::array<std::int16_t, block * 2 + 2> dst;
                dst.fill(12345);
                const int got = beiklive::nds_stub::readStereoPcmBlock(dst.data(), block,
                    [&](std::int16_t *out, int count) {
                        const int n = std::min({count, produced - consumed, 137});
                        std::copy_n(tone.data() + consumed * 2, n * 2, out);
                        consumed += n;
                        return n;
                    });
                assert(got > 0 && got <= block);
                assert(dst[got * 2] == 12345 && dst[got * 2 + 1] == 12345);
                played.insert(played.end(), dst.begin(), dst.begin() + got * 2);
                shorts += got < block;
                ++usedSlots;
            }
            assert(usedSlots <= 4);
            std::int16_t empty[2] = {123, 456};
            assert(beiklive::nds_stub::readStereoPcmBlock(empty, 1,
                [](std::int16_t *, int) { return 0; }) == 0);
            assert(empty[0] == 123 && empty[1] == 456);
        }
        assert(played == tone && shorts > 0);
        int crossings = 0;
        for (int i = 1; i < frames; ++i)
            crossings += played[(i - 1) * 2] <= 0 && played[i * 2] > 0;
        const double duration = double(played.size() / 2) / (rate * speed);
        const double hz = crossings / duration;
        assert(std::abs(hz - 1000 * speed) < 3);
        std::printf("%dx: %d real frames, %.3fs, %.1fHz; PCM unchanged\n", speed, frames, duration, hz);
    }
    // Reproduce the old bug: padding each 548/549-frame fragment to 768
    // stretches two seconds of source audio to about 2.8 seconds.
    int oldFrames = 0;
    for (int i = 0, n = 0; i < frames; ++n) {
        i += std::min(frames - i, 548 + n % 2);
        oldFrames += block;
    }
    assert(oldFrames > frames * 1.39);
    std::printf("old held-sample padding: %.3fs for a 2.000s source (reproduced)\n", double(oldFrames) / rate);

    beiklive::nds_stub::PcmQueue queue;
    queue.setNormalSpeed(true);
    std::vector<std::int16_t> played;
    std::array<std::int16_t, block * 2> out;
    for (int i = 0, batch = 0; i < frames; ++batch) {
        const int n = std::min(549, frames - i);
        queue.push(tone.data() + i * 2, n);
        i += n;
        // Game produces five frames while the sound device consumes one.
        if (batch % 5 == 0) {
            const int got = queue.read(out.data(), 549);
            played.insert(played.end(), out.begin(), out.begin() + got * 2);
        }
    }
    while (const int got = queue.read(out.data(), block))
        played.insert(played.end(), out.begin(), out.begin() + got * 2);
    assert(played == tone);
    assert(double(played.size() / 2) / rate == 2.0);
    std::puts("5x game / normal audio: source stays 2.000s at 1000Hz");

    // A bounded queue retains its old timeline when full, without increasing
    // playback rate. Exercise wraparound and both stereo channels as well.
    std::vector<std::int16_t> full(rate * 30 * 2, 1234);
    queue.push(full.data(), rate * 30);
    queue.push(tone.data(), 549);
    assert(queue.size() == rate * 30);
    assert(queue.read(out.data(), 549) == 549 && out[0] == 1234);
    queue.push(tone.data(), 549);
    std::vector<std::int16_t> wrapped(full.size());
    assert(queue.read(wrapped.data(), rate * 30) == rate * 30);
    assert(std::equal(tone.begin(), tone.begin() + 549 * 2, wrapped.end() - 549 * 2));
    queue.push(tone.data(), 549);
    queue.setNormalSpeed(false);
    assert(queue.size() == 0); // fast-forward exit discards delayed effects
    queue.push(tone.data(), frames);
    std::vector<std::int16_t> live(4096 * 2);
    assert(queue.read(live.data(), 4096) == 4096);
    assert(std::equal(live.begin(), live.end(), tone.end() - live.size()));
    queue.push(tone.data(), 549);
    queue.clear(); // pause / load / mute
    assert(queue.read(out.data(), block) == 0);
    std::puts("audio queue: wraparound, overflow and mode/reset flush passed");
}
