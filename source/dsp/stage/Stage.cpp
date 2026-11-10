#include "dsp/stage/Stage.h"

#include <cmath>

namespace stage
{
    namespace
    {
        constexpr float speedOfSound = 343.0f;
        constexpr float pointWeight = 0.5f;   // each of a part's two points into each mic (a mono part in the middle: its own level)
        constexpr float micSpacing = 2.0f, micHeight = 3.2f, sourceHeight = 1.2f;   // (2 m: as a main pair's outer mics)
        constexpr float referenceY = 5.0f;
        constexpr float maxSlew = 0.04f;      // a moving tap's delay: samples a sample at most (about 2/3 of a semitone)    // the seat a part keeps its level at: the middle, 5 m back
        constexpr std::array<float, 8> lineMilliseconds { 31.1f, 37.3f, 41.9f, 47.1f, 53.6f, 59.3f, 67.7f, 73.9f };

        float distance (float ax, float ay, float az, float bx, float by, float bz) noexcept
        {
            return std::sqrt ((ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz));
        }

        float onePole (double hz, double rate) noexcept
        {
            return (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jlimit (20.0, 0.45 * rate, hz) / rate);
        }

        // An 8-point Hadamard transform, scaled to keep the energy (1 / sqrt 8).
        inline void hadamard (std::array<float, 8>& v) noexcept
        {
            for (int h = 1; h < 8; h *= 2)
                for (int i = 0; i < 8; i += 2 * h)
                    for (int j = i; j < i + h; ++j)
                    {
                        const auto a = v[(size_t) j], b = v[(size_t) (j + h)];
                        v[(size_t) j] = a + b;
                        v[(size_t) (j + h)] = a - b;
                    }
            for (auto& x : v)
                x *= 0.35355339f;
        }
    }

    const Room& roomFor (int index) noexcept
    {
        static const std::array<Room, 4> rooms { {
            { 9.5f,  5.0f, 11.6f, -12.0f, 0.55f, 0.5f, 3.0f, 0.45f, 7000.0f },   // Studio
            { 13.0f, 11.0f, 13.0f, -22.0f, 0.65f, 1.4f, 5.0f, 0.8f,  9000.0f },  // Scoring Stage
            { 14.0f, 16.0f, 11.6f, -35.0f, 0.70f, 2.1f, 7.0f, 1.2f,  6500.0f },  // Concert Hall (the shell behind the stage)
            { 17.0f, 28.0f, 20.0f, -60.0f, 0.80f, 4.8f, 9.0f, 2.2f,  4500.0f },  // Cathedral
        } };
        return rooms[(size_t) juce::jlimit (0, 3, index)];
    }

    float riserHeight (float x, float y) noexcept
    {
        // The risers' fronts are arcs round a point 6 m in front of the stage (as stage3d.py draws them).
        for (const auto& [radius, height] : { std::pair { 15.5f, 0.6f }, std::pair { 14.0f, 0.4f }, std::pair { 12.5f, 0.2f } })
            if (std::abs (x) < radius && y >= -6.0f + std::sqrt (radius * radius - x * x))
                return height;
        return 0.0f;
    }

    Stage::Stage() { prepare (48000.0, 512); }

    void Stage::prepare (double sampleRate, int maxBlockSize)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        blockSize = juce::jmax (1, maxBlockSize);
        ringSize = juce::nextPowerOfTwo ((int) std::ceil (maxSeconds * rate) + blockSize + 8);
        ringMask = ringSize - 1;
        rings.assign ((size_t) maxParts * 2 * (size_t) ringSize, 0.0f);
        lineSize = juce::nextPowerOfTwo ((int) std::ceil (0.08 * 2.4 * rate) + 8);   // the longest line in the biggest room
        lineMask = lineSize - 1;
        lines.assign ((size_t) numLines * (size_t) lineSize, 0.0f);
        scratch.assign ((size_t) 7 * (size_t) blockSize, 0.0f);
        reset();
    }

    void Stage::reset() noexcept
    {
        std::fill (rings.begin(), rings.end(), 0.0f);
        std::fill (lines.begin(), lines.end(), 0.0f);
        for (auto& part : airState) part.fill (0.0f);
        for (auto& part : wallState) part.fill (0.0f);
        lineDamp.fill (0.0f);
        quiet.fill (1 << 30);
        writePos = linePos = 0;
        aimed = false;
        shownRoom = -1;
    }

    void Stage::setSeats (const std::array<Seat, maxParts>& s, int count) noexcept
    {
        sequence.fetch_add (1, std::memory_order_acq_rel);   // odd: being written
        pendingSeats = s;
        pendingCount.store (juce::jlimit (0, maxParts, count), std::memory_order_relaxed);
        sequence.fetch_add (1, std::memory_order_acq_rel);   // even: done
    }

    Stage::Tap Stage::directTap (int part, int point, int mic) const noexcept
    {
        const auto& g = taps[(size_t) juce::jlimit (0, maxParts - 1, part)][(size_t) (point & 1)][(size_t) (mic & 1)][0];
        return { g.targetDelay, g.targetGain };
    }

    void Stage::aim (const Settings& settings) noexcept
    {
        const auto& room = roomFor (settings.room);
        const auto spread = juce::jmax (0.0f, settings.spread);
        const auto micY = -juce::jmax (0.5f, settings.micDistance);
        const std::array<float, 2> micX { -0.5f * micSpacing, 0.5f * micSpacing };
        // Each mic aimed outwards: at a point 5 m to its side, 5 m back on the stage.
        const auto cardioid = [&] (int mic, float x, float y, float z)
        {
            const auto ax = (mic == 0 ? -5.0f : 5.0f) - micX[(size_t) mic], ay = referenceY - micY, az = sourceHeight - micHeight;
            const auto vx = x - micX[(size_t) mic], vy = y - micY, vz = z - micHeight;
            const auto cosine = (ax * vx + ay * vy + az * vz) / juce::jmax (1.0e-6f, std::sqrt ((ax * ax + ay * ay + az * az) * (vx * vx + vy * vy + vz * vz)));
            return juce::jmax (0.0f, 0.37f + 0.63f * cosine);   // (a little tighter than a cardioid: clearer seats)
        };
        // The level kept: a part in the middle, 5 m back, through the left mic directly.
        const auto reference = distance (0.0f, referenceY, sourceHeight, micX[0], micY, micHeight);
        const auto norm = reference / cardioid (0, 0.0f, referenceY, sourceHeight);
        // The delay to the stage's front taken off.
        const auto offset = (distance (0.0f, 0.0f, sourceHeight, 0.0f, micY, micHeight) - 0.3f) / speedOfSound * (float) rate;
        const auto longest = (float) (maxSeconds * rate);
        const auto roomLevel = juce::Decibels::decibelsToGain (settings.roomLevelDb);
        const auto toDelay = [&] (float metres) { return juce::jlimit (1.0f, longest, metres / speedOfSound * (float) rate - offset); };

        for (int p = 0; p < maxParts; ++p)
        {
            const auto& seat = seats[(size_t) p];
            const auto x = seat.x * spread, quarter = 0.25f * seat.width * spread, y = seat.y;
            // The images of a point in the six surfaces: floor, ceiling, left and right walls, the wall behind the
            // stage, the wall behind the mics (the point itself first).
            const auto imagesOf = [&room] (float px, float py, float pz)
            {
                return std::array<std::array<float, 3>, numTaps> { {
                    { px, py, pz }, { px, py, -pz }, { px, py, 2.0f * room.height - pz },
                    { -2.0f * room.halfWidth - px, py, pz }, { 2.0f * room.halfWidth - px, py, pz },
                    { px, 2.0f * room.backWall - py, pz }, { px, 2.0f * room.frontWall - py, pz } } };
            };
            // The seat's middle sets every path's length: a part's two sides travel together (two copies of much the
            // same sound arriving apart would cancel at some pitches: a hollow, phasey colour). Its width comes from
            // their levels: each side placed at its edge of the seat, quieter in the mic it's farther from or off the
            // aim of.
            const auto cx = juce::jlimit (-halfWidth, halfWidth, x), cz = sourceHeight + riserHeight (cx, y);
            const auto middle = imagesOf (cx, y, cz);
            for (int point = 0; point < 2; ++point)
            {
                const auto px = juce::jlimit (-halfWidth, halfWidth, x + (point == 0 ? -quarter : quarter));
                const auto pz = sourceHeight + riserHeight (px, y);
                const auto sources = imagesOf (px, y, pz);
                for (int mic = 0; mic < 2; ++mic)
                    for (int t = 0; t < numTaps; ++t)
                    {
                        const auto& s = sources[(size_t) t];
                        const auto& m = middle[(size_t) t];
                        const auto d = juce::jmax (0.3f, distance (s[0], s[1], s[2], micX[(size_t) mic], micY, micHeight));
                        auto& glide = taps[(size_t) p][(size_t) point][(size_t) mic][(size_t) t];
                        glide.targetDelay = toDelay (distance (m[0], m[1], m[2], micX[(size_t) mic], micY, micHeight));
                        glide.targetGain = p < seatCount ? norm * pointWeight * cardioid (mic, s[0], s[1], s[2]) / d
                                                               * (t == 0 ? 1.0f : room.reflection * roomLevel)
                                                         : 0.0f;
                    }
                auto& tail = tailTaps[(size_t) p][(size_t) point];
                tail.targetDelay = toDelay (distance (cx, y, cz, 0.0f, micY, micHeight));
                tail.targetGain = p < seatCount ? 0.5f : 0.0f;
            }
            // The air: the highs dulled with the distance to each mic.
            for (int mic = 0; mic < 2; ++mic)
            {
                const auto d = distance (x, y, sourceHeight, micX[(size_t) mic], micY, micHeight);
                airCoef[(size_t) p][(size_t) mic] = onePole (16000.0 * std::exp (-d / 40.0), rate);
            }
        }
        wallCoef = onePole (room.damping, rate);

        // The tail: its lines for the room, each falling 60 dB in the room's time x DECAY.
        const auto seconds = room.seconds * juce::jlimit (0.25f, 4.0f, settings.decay);
        if (settings.room != shownRoom)
        {
            std::fill (lines.begin(), lines.end(), 0.0f);   // (a new room: its tail starts afresh)
            lineDamp.fill (0.0f);
            shownRoom = settings.room;
        }
        auto meanSquare = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            lineLength[(size_t) i] = juce::jlimit (8, lineSize - 1, (int) std::lround (lineMilliseconds[(size_t) i] * room.scale * 0.001 * rate));
            lineGain[(size_t) i] = std::pow (10.0f, -3.0f * (float) lineLength[(size_t) i] / (seconds * (float) rate));
            meanSquare += lineGain[(size_t) i] * lineGain[(size_t) i] / (float) numLines;
        }
        // Its level: equal to a part's direct sound at the room's critical distance (scaled as the direct sound is), so
        // farther mics hear more room; ROOM LEVEL on top. sqrt (1 - g^2) undoes the lines' build-up.
        tailOutTarget = reference / room.criticalDistance * roomLevel * std::sqrt (juce::jmax (1.0e-4f, 1.0f - meanSquare)) * 2.0f;
        if (! aimed)   // (the first aim after a start lands at once: only later changes glide)
        {
            for (auto& part : taps) for (auto& point : part) for (auto& mic : point) for (auto& g : mic) { g.delay = g.targetDelay; g.gain = g.targetGain; }
            for (auto& part : tailTaps) for (auto& g : part) { g.delay = g.targetDelay; g.gain = g.targetGain; }
            tailOut = tailOutTarget;
        }
        shown = settings;
        aimed = true;
    }

    void Stage::process (const std::array<const float*, maxParts>& left, const std::array<const float*, maxParts>& right, int numParts,
                         float* outLeft, float* outRight, int numSamples, const Settings& settings) noexcept
    {
        numParts = juce::jlimit (0, maxParts, numParts);
        // The seats, if the message thread has given new ones.
        auto changed = ! aimed;
        if (const auto before = sequence.load (std::memory_order_acquire); (before & 1u) == 0)
        {
            const auto copy = pendingSeats;
            const auto count = pendingCount.load (std::memory_order_relaxed);
            if (sequence.load (std::memory_order_acquire) == before && (count != seatCount || copy != seats))
            {
                seats = copy;
                seatCount = count;
                changed = true;
            }
        }
        changed = changed || settings.room != shown.room || settings.micDistance != shown.micDistance || settings.roomLevelDb != shown.roomLevelDb
               || settings.decay != shown.decay || settings.spread != shown.spread;
        if (changed)
            aim (settings);

        for (int done = 0; done < numSamples;)
        {
            const auto n = juce::jmin (blockSize, numSamples - done);
            auto* tailIn = buffer (4);
            std::fill (tailIn, tailIn + n, 0.0f);
            for (int p = 0; p < numParts; ++p)
            {
                // Its sound into its rings.
                auto loudest = 0.0f;
                for (int point = 0; point < 2; ++point)
                {
                    const auto* from = (point == 0 ? left : right)[(size_t) p];
                    auto* r = ring (p, point);
                    for (int i = 0; i < n; ++i)
                    {
                        const auto x = from != nullptr ? from[done + i] : 0.0f;
                        r[(writePos + i) & ringMask] = x;
                        loudest = juce::jmax (loudest, std::abs (x));
                    }
                }
                quiet[(size_t) p] = loudest > 1.0e-7f ? 0 : juce::jmin (1 << 30, quiet[(size_t) p] + n);
                if (quiet[(size_t) p] > ringSize)   // silent for longer than any tap reaches back: nothing to hear
                {
                    for (auto& point : taps[(size_t) p]) for (auto& mic : point) for (auto& g : mic) { g.delay = g.targetDelay; g.gain = g.targetGain; }
                    for (auto& g : tailTaps[(size_t) p]) { g.delay = g.targetDelay; g.gain = g.targetGain; }
                    continue;
                }
                std::array<float*, 2> direct { buffer (0), buffer (1) }, walls { buffer (2), buffer (3) };
                for (int mic = 0; mic < 2; ++mic)
                {
                    std::fill (direct[(size_t) mic], direct[(size_t) mic] + n, 0.0f);
                    std::fill (walls[(size_t) mic], walls[(size_t) mic] + n, 0.0f);
                }
                const auto read = [&] (Glide& g, const float* r, float* into)
                {
                    // The tap's level glides to its target across the block; its delay moves towards its target at
                    // most `maxSlew` samples a sample (the pitch bends under 2/3 of a semitone while a seat travels: a
                    // long move takes a little longer, as a player walking over, rather than a chirp).
                    const auto gainStep = (g.targetGain - g.gain) / (float) n;
                    auto gain = g.gain;
                    if (gain == 0.0f && gainStep == 0.0f)
                    {
                        g.delay = g.targetDelay;
                        return;
                    }
                    if (g.delay == g.targetDelay)
                    {
                        // Still: read at the nearest whole sample (reading between two averages them: duller highs,
                        // by an amount that would differ from seat to seat; the nearest is under 0.01 ms out).
                        const auto back = (int) std::lround (g.delay);
                        for (int i = 0; i < n; ++i)
                        {
                            into[i] += gain * r[(writePos + i - back) & ringMask];
                            gain += gainStep;
                        }
                    }
                    else
                    {
                        auto delay = g.delay;
                        for (int i = 0; i < n; ++i)
                        {
                            delay += juce::jlimit (-maxSlew, maxSlew, g.targetDelay - delay);
                            const auto at = (float) (writePos + i) - delay;
                            const auto whole = (int) std::floor (at);
                            const auto t = at - (float) whole;
                            const auto a = r[whole & ringMask], b = r[(whole + 1) & ringMask];
                            into[i] += gain * (a + (b - a) * t);
                            gain += gainStep;
                        }
                        g.delay = std::abs (g.targetDelay - delay) < 1.0e-3f ? g.targetDelay : delay;
                    }
                    g.gain = g.targetGain;
                };
                for (int point = 0; point < 2; ++point)
                {
                    const auto* r = ring (p, point);
                    for (int mic = 0; mic < 2; ++mic)
                    {
                        auto& set = taps[(size_t) p][(size_t) point][(size_t) mic];
                        read (set[0], r, direct[(size_t) mic]);
                        for (int t = 1; t < numTaps; ++t)
                            read (set[(size_t) t], r, walls[(size_t) mic]);
                    }
                    read (tailTaps[(size_t) p][(size_t) point], r, tailIn);
                }
                // The air on the direct sound, the walls' darkening on the reflections; into the mics.
                for (int mic = 0; mic < 2; ++mic)
                {
                    auto* out = (mic == 0 ? outLeft : outRight) + done;
                    const auto air = airCoef[(size_t) p][(size_t) mic];
                    auto& airMemory = airState[(size_t) p][(size_t) mic];
                    auto& wallMemory = wallState[(size_t) p][(size_t) mic];
                    for (int i = 0; i < n; ++i)
                    {
                        airMemory = direct[(size_t) mic][i] + air * (airMemory - direct[(size_t) mic][i]);
                        wallMemory = walls[(size_t) mic][i] + wallCoef * (wallMemory - walls[(size_t) mic][i]);
                        out[i] += airMemory + wallMemory;
                    }
                }
            }
            // The shared tail.
            const auto outStep = (tailOutTarget - tailOut) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                std::array<float, numLines> v {};
                for (int k = 0; k < numLines; ++k)
                {
                    const auto y = lines[(size_t) k * (size_t) lineSize + (size_t) ((linePos - lineLength[(size_t) k]) & lineMask)];
                    auto& damp = lineDamp[(size_t) k];
                    damp = y + wallCoef * (damp - y);
                    v[(size_t) k] = damp * lineGain[(size_t) k];
                }
                const auto outL = 0.5f * (v[0] - v[2] + v[4] - v[6]), outR = 0.5f * (v[1] - v[3] + v[5] - v[7]);
                hadamard (v);
                const auto x = tailIn[i] * 0.35355339f;
                for (int k = 0; k < numLines; ++k)
                    lines[(size_t) k * (size_t) lineSize + (size_t) linePos] = v[(size_t) k] + ((k & 1) != 0 ? -x : x);
                linePos = (linePos + 1) & lineMask;
                tailOut += outStep;
                outLeft[done + i] += outL * tailOut;
                outRight[done + i] += outR * tailOut;
            }
            tailOut = tailOutTarget;
            writePos = (writePos + n) & ringMask;
            done += n;
        }
    }
}
