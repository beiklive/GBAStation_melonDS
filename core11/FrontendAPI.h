#pragma once
#include "Session.h"
#include "Video.h"
// Frontend operations used by the shared input/audio loop.
namespace NDS {
inline void SetKeyMask(unsigned m) { Core11::Keys(m); }
inline void TouchScreen(int x, int y) { Core11::Touch(x, y); }
inline void ReleaseScreen() { Core11::Touch(-1, 0); }
inline void SetLidClosed(bool closed) { Core11::Lid(closed); }
inline void MicInputFrame(const std::int16_t *data, int count) { Core11::Microphone(data, count); }
} // namespace NDS
namespace SPU {
inline int ReadOutput(std::int16_t *data, int count) { return Core11::Audio(data, count); }
inline void DrainOutput() { Core11::DrainAudio(); }
} // namespace SPU
