#pragma once
#include <cstdint>

namespace beiklive::nds_stub {
// A partial read is real audio, not a full wave buffer. Never invent samples
// to fill the allocation: doing so changes its duration and breaks pitch.
template <class Reader>
int readStereoPcmBlock(std::int16_t *out, int capacity, Reader &&read)
{
    int frames = 0;
    while (frames < capacity)
    {
        const int got = read(out + frames * 2, capacity - frames);
        if (got <= 0)
            break;
        frames += got;
    }
    return frames;
}
} // namespace beiklive::nds_stub
