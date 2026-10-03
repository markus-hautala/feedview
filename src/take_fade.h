// The fade between sources when the operator takes a new one.
//
//  * From a picture to another source: the old picture stays live (with its sound) until the
//    new source's first frame arrives, then the two dissolve over the fade time. So the
//    output never goes black while the new source connects. A source that sends nothing
//    within kMaxWaitMs is faded to anyway, which means fading the old picture to black.
//  * To "None": the old picture fades straight to black.
//  * From black (None, or a source that wasn't sending): the new source's first frame
//    fades in from black.
//  * A fade time of 0 is a cut - still at the moment the new source's picture is there.
// Sound crossfades with the picture; it's not held back for a source without video.
//
// Pure logic with no SDL, covered by tests/live_control_test.cpp; main.cpp keeps the old
// receiver running while active() and draws/mixes with the values from update().
#pragma once

#include <cstdint>

class TakeFade {
public:
    static constexpr uint64_t kMaxWaitMs = 3000;
    static constexpr int kMaxFadeMs = 10000;

    // A take while a picture is on screen. `waitForPicture`: false when taking None.
    void begin(uint64_t nowMs, uint64_t fadeMs, bool waitForPicture);
    // A take from black: the new source's first frame fades in (0 = it simply appears).
    void fadeInNext(uint64_t fadeMs);
    void reset();
    // The new source's first frame arrived.
    void firstFrame(uint64_t nowMs);

    struct Mix {
        float outgoing = 0.0f;  // opacity of the old picture
        float incoming = 1.0f;  // opacity of the new picture
        float soundOut = 0.0f;  // gain of the old source's sound
        float soundIn = 1.0f;   // gain of the new source's sound
        bool waiting = false;   // the old picture is held, waiting for the new source
        bool finished = false;  // just ended: the old source can be dropped (reported once)
    };
    Mix update(uint64_t nowMs);
    bool active() const { return active_; }  // an old picture/sound is still part of the mix

private:
    bool active_ = false;
    bool incomingSeen_ = false;  // the new source delivered a frame during this take
    uint64_t fadeMs_ = 0, takenAt_ = 0, fadeStart_ = 0;  // fadeStart_ 0 = still waiting
    bool fadeInPending_ = false;
    uint64_t fadeInMs_ = 0, fadeInStart_ = 0;  // fadeInStart_ 0 = no fade-in running
};
