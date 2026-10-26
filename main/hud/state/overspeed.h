#pragma once
#include "config/device_config.h"
#include "state/hud_state.h"
#include <algorithm>
namespace waze_hud {
inline int adjustedSpeed(int speed, const DeviceSettings&) { return speed; }
inline bool overspeedActive(const HudState& s, const DeviceSettings& c) {
 if (!s.connected || !s.hasProducerState || s.signalStale || s.speedLimitKmh <= 0) return false;
 return s.speedKmh >= std::max(0, s.speedLimitKmh + int(c.overspeedOffsetKmh));
}
}
