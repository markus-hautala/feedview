#include "take_fade.h"

#include <algorithm>

void TakeFade::begin(uint64_t now, uint64_t fadeMs, bool waitForPicture) {
    active_ = true;
    incomingSeen_ = false;
    fadeMs_ = fadeMs;
    takenAt_ = now;
    fadeStart_ = waitForPicture ? 0 : std::max<uint64_t>(now, 1);
    fadeInPending_ = false;
    fadeInStart_ = 0;
}

void TakeFade::fadeInNext(uint64_t fadeMs) {
    reset();
    fadeInPending_ = fadeMs > 0;
    fadeInMs_ = fadeMs;
}

void TakeFade::reset() {
    active_ = false;
    fadeInPending_ = false;
    fadeInStart_ = 0;
}

void TakeFade::firstFrame(uint64_t now) {
    if (active_) {
        incomingSeen_ = true;
        if (!fadeStart_) fadeStart_ = std::max<uint64_t>(now, 1);
    } else if (fadeInPending_) {
        fadeInPending_ = false;
        fadeInStart_ = std::max<uint64_t>(now, 1);
    }
}

TakeFade::Mix TakeFade::update(uint64_t now) {
    Mix m;
    if (active_) {
        if (!fadeStart_ && now - takenAt_ >= kMaxWaitMs) fadeStart_ = std::max<uint64_t>(now, 1);
        if (!fadeStart_) {
            m.outgoing = 1.0f;
            m.incoming = 0.0f;
            m.soundOut = 1.0f;
            m.soundIn = 0.0f;
            m.waiting = true;
            return m;
        }
        const float a = fadeMs_ ? std::clamp(float(now - fadeStart_) / float(fadeMs_), 0.0f, 1.0f) : 1.0f;
        if (a >= 1.0f) {
            active_ = false;
            m.finished = true;
            // Faded to black because the new source sent nothing yet: when it does, fade it in.
            if (!incomingSeen_) fadeInNext(fadeMs_);
            return m;
        }
        m.outgoing = m.soundOut = 1.0f - a;
        m.incoming = m.soundIn = a;
        return m;
    }
    if (fadeInStart_) {
        const float a = std::clamp(float(now - fadeInStart_) / float(fadeInMs_), 0.0f, 1.0f);
        if (a >= 1.0f) fadeInStart_ = 0;
        m.incoming = a;
    }
    return m;
}
