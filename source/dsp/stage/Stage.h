#pragma once

#include "dsp/stage/Seating.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

// THE STAGE's SOUND (PR7-O 0.8.0, our own design after PR7-D's MicSim; the owner: "Build it"). Each part of what's
// loaded heard from its seat (stage::Seat) through a pair of mics in front of the stage, in a room, so parts recorded
// apart sit in one space. Nothing in it is measured from a real hall or microphone:
//
//   - A part sits at its seat, 1.2 m up (plus its riser). Its recording's left side is heard from the seat's left edge
//     and its right side from its right (the seat's width, times SPREAD; SPREAD also draws the seats towards the
//     middle), by level only: both travel the seat's middle's paths (two copies of much the same sound arriving apart
//     would cancel at some pitches: a hollow, phasey colour).
//   - The mics: a pair 2 m apart (as a main pair's outer mics), 3.2 m up, MIC metres in front of the stage, each aimed
//     outwards and a little tighter than a cardioid (0.37 + 0.63 cos of the angle off its aim: a sound to its side
//     quieter; VSCO's strings, each section alone, about 4 dB to its side at the defaults, with the mics' timing).
//   - Each point reaches each mic directly (343 m/s; level 1 / distance; the highs dulled with distance, the air) and
//     off six surfaces (image sources: the floor, the ceiling, the side walls, the wall behind the stage and the one
//     behind the mics; each reflection quieter by the room's absorption and darker).
//   - Then one shared tail: a feedback delay network (8 lines, as PR7-D's MicSim's room) every part feeds, its time
//     the ROOM's times DECAY, its level set so that at the room's "critical distance" its sound and a part's direct
//     sound are equal (farther mics: more room), times ROOM LEVEL.
//   - Levels are scaled so a part in the middle of the stage (5 m back) comes through the mics at the level it plays
//     at: moving the mics changes the balance of direct and room, not the overall level. The delay to the stage's
//     front is taken off (playing stays tight): only the differences between seats remain.
//   - Levels glide across each block when a seat or a control moves; delays move at most 4% a sample (the pitch bends
//     under 2/3 of a semitone while a seat travels: no clicks, no chirps; a long move takes a moment, as a player
//     walking over).
// Nothing is allocated on the audio thread; the seats come from the message thread through a sequence lock.
namespace stage
{
    inline constexpr int maxParts = 8;

    struct Room
    {
        float halfWidth, height, backWall, frontWall;   // metres (walls: y of the wall behind the stage, behind the mics)
        float reflection;                                // a reflection's level (1 - absorption)
        float seconds;                                   // the tail's time to fall 60 dB
        float criticalDistance;                          // where the direct sound and the tail are equal (metres)
        float scale;                                     // the tail's delay lines, times
        float damping;                                   // the reflections' and tail's darkening (Hz)
    };
    const Room& roomFor (int index) noexcept;   // ParamIDs::stageRoomNames' order

    struct Settings
    {
        int room = 2;
        float micDistance = 2.5f;     // metres in front of the stage's front edge
        float roomLevelDb = -3.0f;
        float decay = 1.0f;           // x the room's time
        float spread = 1.0f;          // x the seats' distance from the middle and their width
    };

    // Where a source sits (metres): x across, y back, z up (1.2 m over the stage's floor or its riser).
    float riserHeight (float x, float y) noexcept;

    class Stage
    {
    public:
        static constexpr int numTaps = 7;    // direct + six reflections, for each of a part's two points to each mic
        static constexpr double maxSeconds = 0.5;

        Stage();
        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;

        // From the message thread: each part's seat (index = the sampler's part).
        void setSeats (const std::array<Seat, maxParts>&, int count) noexcept;

        // Adds the stage's sound of `numParts` parts (their left / right, `numSamples` each) into `outLeft` / `outRight`.
        void process (const std::array<const float*, maxParts>& left, const std::array<const float*, maxParts>& right, int numParts,
                      float* outLeft, float* outRight, int numSamples, const Settings&) noexcept;

        // What a part's two points sound like to the mics now (tests): delays (samples) and gains of its direct taps,
        // [point][mic], with the settings last processed.
        struct Tap { float delay = 0.0f, gain = 0.0f; };
        Tap directTap (int part, int point, int mic) const noexcept;

    private:
        struct Point3 { float x, y, z; };
        void aim (const Settings&) noexcept;   // every tap's target from the seats and settings now

        double rate = 48000.0;
        int blockSize = 512;
        int ringSize = 1, ringMask = 0, writePos = 0;
        // Each part's two points' sound so far (rings).
        std::vector<float> rings;   // maxParts x 2 x ringSize
        float* ring (int part, int point) noexcept { return rings.data() + ((size_t) part * 2 + (size_t) point) * (size_t) ringSize; }
        // The taps: [part][point][mic][tap], where they are and where they're going this block.
        struct Glide { float delay = 1.0f, gain = 0.0f, targetDelay = 1.0f, targetGain = 0.0f; };
        std::array<std::array<std::array<std::array<Glide, numTaps>, 2>, 2>, maxParts> taps {};
        std::array<std::array<Glide, 2>, maxParts> tailTaps {};   // each point into the tail
        // Each part's air (direct) and walls (reflections) filters, per mic: one-pole low-passes.
        std::array<std::array<float, 2>, maxParts> airCoef {}, airState {}, wallState {};
        float wallCoef = 0.0f;
        std::array<int, maxParts> quiet {};   // samples since the part last made a sound (skipped once rings have emptied)
        bool primed = false;

        // The seats, from the message thread (a sequence lock: odd while it's written).
        std::array<Seat, maxParts> pendingSeats {};
        std::atomic<int> pendingCount { 0 };
        std::atomic<unsigned> sequence { 0 };
        std::array<Seat, maxParts> seats {};
        int seatCount = 0;
        Settings shown;
        bool aimed = false;

        // The tail: 8 lines, each with its damping filter.
        static constexpr int numLines = 8;
        std::vector<float> lines;   // numLines x lineSize
        int lineSize = 1, lineMask = 0, linePos = 0;
        std::array<int, numLines> lineLength {};
        std::array<float, numLines> lineGain {}, lineDamp {};
        float tailOut = 0.0f, tailOutTarget = 0.0f;
        int shownRoom = -1;
        float shownDecay = -1.0f;

        // Scratch (made once).
        std::vector<float> scratch;   // 7 x blockSize: direct L/R, walls L/R, tail in, tail out L/R
        float* buffer (int which) noexcept { return scratch.data() + (size_t) which * (size_t) blockSize; }
    };
}
