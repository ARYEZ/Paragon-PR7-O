#include "dsp/sampler/Sfz.h"

#include <set>

namespace sampler
{
    juce::int64 Region::lastFrame() const noexcept
    {
        const auto length = sample != nullptr ? (juce::int64) sample->length() : 0;
        return end >= 0 ? juce::jmin (end, length - 1) : length - 1;
    }

    int Program::lowestKey() const noexcept
    {
        for (int k = 0; k < 128; ++k)
            if (plays (k))
                return k;
        return -1;
    }

    int Program::highestKey() const noexcept
    {
        for (int k = 127; k >= 0; --k)
            if (plays (k))
                return k;
        return -1;
    }

    int Program::velocityLayersAt (int key, int keyswitch) const
    {
        if (key < 0 || key >= 128)
            return 0;
        std::set<std::pair<int, int>> ranges;
        for (const auto i : byKey[(size_t) key])
            if (keyswitch == anyKeyswitch || regions[(size_t) i].playsUnder (keyswitch))
                ranges.insert ({ regions[(size_t) i].loVel, regions[(size_t) i].hiVel });
        return (int) ranges.size();
    }

    int Program::roundRobinsAt (int key, int keyswitch) const
    {
        if (key < 0 || key >= 128)
            return 0;
        std::map<std::pair<int, int>, int> perRange;
        auto most = 0;
        for (const auto i : byKey[(size_t) key])
            if (keyswitch == anyKeyswitch || regions[(size_t) i].playsUnder (keyswitch))
                most = juce::jmax (most, ++perRange[{ regions[(size_t) i].loVel, regions[(size_t) i].hiVel }]);
        return most;
    }

    void Program::index()
    {
        for (auto& list : byKey)
            list.clear();
        for (int i = 0; i < (int) regions.size(); ++i)
        {
            const auto& r = regions[(size_t) i];
            for (int k = juce::jmax (0, r.loKey); k <= juce::jmin (127, r.hiKey); ++k)
                byKey[(size_t) k].push_back (i);
        }
        sequence.assign (regions.size(), 0);
    }

    int Program::articulationOf (int keyswitch) const noexcept
    {
        for (int i = 0; i < (int) articulations.size(); ++i)
            if (keyswitch >= articulations[(size_t) i].lo && keyswitch <= articulations[(size_t) i].hi)
                return i;
        return -1;
    }

    namespace
    {
        // A recording's level in 50 ms windows (both sides), and the median of its middle three fifths.
        struct Levels { std::vector<float> windows; int window = 1; float body = 0.0f; };
        Levels levelsOf (const Sample& sample)
        {
            Levels result;
            result.window = juce::jmax (1, (int) (0.05 * sample.sampleRate));
            const auto count = sample.length() / result.window;
            if (count < 8)
                return result;
            result.windows.resize ((size_t) count);
            for (int w = 0; w < count; ++w)
            {
                double sum = 0.0;
                for (const auto& c : sample.channels)
                    for (int i = w * result.window; i < (w + 1) * result.window; ++i)
                        sum += (double) c[(size_t) i] * c[(size_t) i];
                result.windows[(size_t) w] = (float) std::sqrt (sum / (double) (result.window * (int) sample.channels.size()));
            }
            std::vector<float> middle (result.windows.begin() + count / 5, result.windows.begin() + count * 4 / 5);
            std::nth_element (middle.begin(), middle.begin() + (long) middle.size() / 2, middle.end());
            result.body = middle[middle.size() / 2];
            return result;
        }
    }

    bool isSustained (const Sample& sample)
    {
        const auto levels = levelsOf (sample);
        if (levels.windows.empty() || levels.body < 1.0e-6f)
            return false;
        const auto& w = levels.windows;
        const auto count = (int) w.size();
        const auto peak = *std::max_element (w.begin(), w.end());
        const auto from = count / 5, to = count * 4 / 5, quarter = juce::jmax (1, (to - from) / 4);
        double early = 0.0, late = 0.0;
        for (int i = from; i < from + quarter; ++i)
            early += w[(size_t) i];
        for (int i = to - quarter; i < to; ++i)
            late += w[(size_t) i];
        const auto peakDb = juce::Decibels::gainToDecibels (peak / levels.body);
        const auto fallDb = juce::Decibels::gainToDecibels ((float) ((early + 1.0e-12) / (late + 1.0e-12)));
        return peakDb <= 13.0f && fallDb <= 9.0f;
    }

    SustainPoints findSustain (const Sample& sample)
    {
        SustainPoints points;
        const auto levels = levelsOf (sample);
        if (levels.windows.empty() || levels.body < 1.0e-6f)
            return points;
        const auto rate = sample.sampleRate;
        const auto near = levels.body * 0.7f;   // within 3 dB
        int first = -1, last = -1;
        for (int w = 0; w < (int) levels.windows.size(); ++w)
            if (levels.windows[(size_t) w] >= near)
            {
                if (first < 0)
                    first = w;
                last = w;
            }
        const auto steadyStart = (juce::int64) first * levels.window + (juce::int64) (0.3 * rate);
        const auto steadyEnd = (juce::int64) (last + 1) * levels.window - (juce::int64) (0.1 * rate);
        const auto steady = steadyEnd - steadyStart;
        if (steady < (juce::int64) (1.5 * rate))
            return points;
        const auto span = juce::jmin ((juce::int64) (4.0 * rate), steady * 4 / 5);
        points.found = true;
        points.end = steadyEnd;
        points.start = steadyEnd - span;
        points.crossfade = (int) juce::jmin ((juce::int64) (1.0 * rate), span * 2 / 5);
        // The start moved to where the sound a loop's length before the crossfade's middle best matches it (mono, a
        // 40 ms window): coarse steps of 4 frames, then each frame around the best.
        const auto window = juce::jmax (16, (int) (0.04 * rate)), reach = (int) (0.025 * rate);
        const auto middle = points.end - points.crossfade / 2 - window / 2;
        const auto mono = [&sample] (juce::int64 i)
        {
            float sum = 0.0f;
            for (const auto& c : sample.channels)
                sum += c[(size_t) i];
            return sum;
        };
        std::vector<float> here ((size_t) window);
        for (int i = 0; i < window; ++i)
            here[(size_t) i] = mono (middle + i);
        const auto likenessAt = [&] (int shift)
        {
            // The frames a loop's length earlier, the loop's start moved by `shift`.
            const auto back = middle - (points.end - (points.start + shift));
            if (back < 0 || back + window > sample.length())
                return -2.0;
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int i = 0; i < window; ++i)
            {
                const auto x = (double) here[(size_t) i], y = (double) mono (back + i);
                xy += x * y;
                xx += x * x;
                yy += y * y;
            }
            return xx > 0.0 && yy > 0.0 ? xy / std::sqrt (xx * yy) : 0.0;
        };
        auto best = 0;
        auto bestLikeness = -3.0;
        for (int shift = -reach; shift <= reach; shift += 4)
            if (const auto l = likenessAt (shift); l > bestLikeness)
                bestLikeness = l, best = shift;
        for (int shift = best - 4; shift <= best + 4; ++shift)
            if (const auto l = likenessAt (shift); l > bestLikeness)
                bestLikeness = l, best = shift;
        points.start = juce::jlimit<juce::int64> ((juce::int64) points.crossfade, points.end - 2, points.start + best);
        points.likeness = (float) loopLikeness (sample, points.start, points.end, points.crossfade);
        return points;
    }

    float loopLikeness (const Sample& sample, juce::int64 loopStart, juce::int64 loopEnd, int crossfade)
    {
        // Their correlation over the crossfade: the frames before the loop's end against those before its start.
        if (crossfade <= 0 || loopStart < crossfade || loopEnd > sample.length())
            return 0.0f;
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (const auto& c : sample.channels)
            for (juce::int64 i = 0; i < crossfade; ++i)
            {
                const auto x = (double) c[(size_t) (loopEnd - crossfade + i)], y = (double) c[(size_t) (loopStart - crossfade + i)];
                xy += x * y;
                xx += x * x;
                yy += y * y;
            }
        return xx > 0.0 && yy > 0.0 ? (float) juce::jlimit (-1.0, 1.0, xy / std::sqrt (xx * yy)) : 0.0f;
    }

    void prepareRegions (Program& program)
    {
        // Each sample's shape and points worked out once, however many regions play it.
        std::map<const Sample*, SustainPoints> found;
        std::map<const Sample*, bool> shapes;
        const auto sustained = [&shapes] (const Sample& s)
        {
            auto it = shapes.find (&s);
            if (it == shapes.end())
                it = shapes.emplace (&s, isSustained (s)).first;
            return it->second;
        };
        for (auto& r : program.regions)
        {
            if (r.sample == nullptr)
                continue;
            r.held = ! r.oneShot && (double) r.sample->length() / r.sample->sampleRate > 2.5 && sustained (*r.sample);
            r.autoLoop = false;
            if (r.loop != Region::Loop::none)
            {
                // The file's own loop: its crossfade's curve from how alike its two sides are.
                r.loopLikeness = loopLikeness (*r.sample, r.loopStart, r.loopEnd, r.loopCrossfade);
                continue;
            }
            if (! r.held)
                continue;
            auto it = found.find (r.sample.get());
            if (it == found.end())
                it = found.emplace (r.sample.get(), findSustain (*r.sample)).first;
            const auto& points = it->second;
            if (points.found && points.start > r.offset && points.end <= r.lastFrame())
            {
                r.autoLoop = true;
                r.loopStart = points.start;
                r.loopEnd = points.end;
                r.loopCrossfade = points.crossfade;
                r.loopLikeness = points.likeness;
            }
        }
    }

    std::shared_ptr<Program> programFrom (std::shared_ptr<const Sample> sample, int keyCenter)
    {
        auto program = std::make_shared<Program>();
        if (sample == nullptr || sample->length() < 2)
            return program;
        program->name = sample->name;
        program->source = juce::File (sample->path);
        program->bytes = sample->bytes();
        program->numFiles = 1;
        Region r;
        r.sample = std::move (sample);
        r.keyCenter = keyCenter;
        program->regions.push_back (std::move (r));
        prepareRegions (*program);
        program->index();
        return program;
    }

    namespace sfz
    {
        namespace
        {
            using Opcodes = std::map<juce::String, juce::String>;

            // The file's text, comments out, #include read in (up to 8 deep), #define put in. (PR7-D 0.19.0's.)
            juce::String preprocess (const juce::File& file, std::map<juce::String, juce::String>& defines, int depth, juce::StringArray& problems)
            {
                if (depth > 8 || ! file.existsAsFile())
                {
                    problems.add ("missing: " + file.getFullPathName());
                    return {};
                }
                auto text = file.loadFileAsString();
                juce::String out;
                for (int i = 0; i < text.length();)
                {
                    if (text[i] == '/' && i + 1 < text.length() && text[i + 1] == '*')
                    {
                        const auto end = text.indexOf (i + 2, "*/");
                        i = end < 0 ? text.length() : end + 2;
                    }
                    else if (text[i] == '/' && i + 1 < text.length() && text[i + 1] == '/')
                    {
                        while (i < text.length() && text[i] != '\n')
                            ++i;
                    }
                    else
                        out << text[i++];
                }
                juce::String result;
                for (auto line : juce::StringArray::fromLines (out))
                {
                    const auto trimmed = line.trim();
                    if (trimmed.startsWith ("#define"))
                    {
                        const auto rest = trimmed.substring (7).trim();
                        defines[rest.upToFirstOccurrenceOf (" ", false, false).trim()] = rest.fromFirstOccurrenceOf (" ", false, false).trim();
                        continue;
                    }
                    if (trimmed.startsWith ("#include"))
                    {
                        const auto name = trimmed.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false);
                        result << preprocess (file.getParentDirectory().getChildFile (name.replaceCharacter ('\\', '/')), defines, depth + 1, problems) << "\n";
                        continue;
                    }
                    // Longest names first, so $KIT2 isn't read as $KIT then 2.
                    std::vector<std::pair<juce::String, juce::String>> sorted (defines.begin(), defines.end());
                    std::sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
                    for (const auto& [k, v] : sorted)
                        line = line.replace (k, v);
                    result << line << "\n";
                }
                return result;
            }

            // The headers and their opcodes, each region with everything it inherits, and the <control> in force where it's
            // written (its opcodes as "control.<name>": 0.2.0, a new <control> replaces the one before).
            std::vector<Opcodes> parse (const juce::String& text)
            {
                std::vector<Opcodes> regions;
                Opcodes control, global, master, group, current;
                juce::String header;
                const auto flush = [&]
                {
                    if (header == "region")
                    {
                        Opcodes r;
                        for (const auto& [k, v] : control)
                            r["control." + k] = v;
                        for (const auto* level : { &global, &master, &group, &current })
                            for (const auto& [k, v] : *level)
                                r[k] = v;
                        regions.push_back (std::move (r));
                    }
                    else if (header == "control")
                        control = current;
                    else if (header == "global")
                        global = current;
                    else if (header == "master")
                        master = current;
                    else if (header == "group")
                        group = current;
                    current.clear();
                };
                for (const auto& line : juce::StringArray::fromLines (text))
                {
                    int i = 0;
                    const auto n = line.length();
                    while (i < n)
                    {
                        while (i < n && juce::CharacterFunctions::isWhitespace (line[i]))
                            ++i;
                        if (i >= n)
                            break;
                        if (line[i] == '<')
                        {
                            const auto end = line.indexOfChar (i, '>');
                            if (end < 0)
                                break;
                            flush();
                            header = line.substring (i + 1, end).trim().toLowerCase();
                            if (header == "master")
                                group.clear();
                            if (header == "global")
                                master.clear(), group.clear();
                            i = end + 1;
                            continue;
                        }
                        const auto eq = line.indexOfChar (i, '=');
                        if (eq < 0)
                            break;
                        const auto key = line.substring (i, eq).trim().toLowerCase();
                        // The value: to the next "name=" or '<' on the line (a sample's path may have spaces).
                        auto j = eq + 1;
                        auto valueEnd = n;
                        for (auto k = j; k < n; ++k)
                        {
                            if (line[k] == '<')
                            {
                                valueEnd = k;
                                break;
                            }
                            if (line[k] == '=')
                            {
                                auto back = k - 1;
                                while (back > j && ! juce::CharacterFunctions::isWhitespace (line[back]))
                                    --back;
                                valueEnd = back;
                                break;
                            }
                        }
                        current[key] = line.substring (j, valueEnd).trim();
                        i = valueEnd;
                    }
                }
                flush();
                return regions;
            }

            const juce::String* find (const Opcodes& op, const char* key)
            {
                const auto it = op.find (key);
                return it != op.end() ? &it->second : nullptr;
            }
            float number (const Opcodes& op, const char* key, float otherwise)
            {
                const auto* v = find (op, key);
                return v != nullptr ? v->getFloatValue() : otherwise;
            }

            // Opcodes read here (their aliases too), and ones that don't change the sound (names, hints).
            const std::set<juce::String>& readOpcodes()
            {
                static const std::set<juce::String> names {
                    "sample", "key", "lokey", "hikey", "pitch_keycenter", "lovel", "hivel", "lochan", "hichan",
                    "seq_length", "seq_position", "lorand", "hirand", "tune", "pitch", "transpose", "pitch_keytrack",
                    "volume", "gain", "amplitude", "pan", "amp_veltrack", "offset", "end",
                    "ampeg_delay", "ampeg_attack", "ampeg_hold", "ampeg_decay", "ampeg_sustain", "ampeg_release",
                    "bend_up", "bend_down", "trigger", "default_path", "note_offset", "octave_offset",
                    "sw_lokey", "sw_hikey", "sw_last", "sw_lolast", "sw_hilast", "sw_default", "sw_label",
                    "loop_mode", "loopmode", "loop_start", "loopstart", "loop_end", "loopend", "loop_crossfade", "ampeg_dynamic" };
                return names;
            }
            bool doesNothingToSound (const juce::String& opcode)
            {
                return opcode.startsWith ("control.") || opcode.endsWith ("_label") || opcode.startsWith ("label_cc") || opcode.startsWith ("hint_")
                    || opcode == "group" || opcode == "output" || opcode == "polyphony" || opcode == "note_polyphony";
            }
        }

        int noteNumber (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();
            if (t.isEmpty())
                return -1;
            if (juce::CharacterFunctions::isDigit (t[0]) || t[0] == '-')
                return t.getIntValue();
            // A name: c4 = 60 (SFZ's middle C), with # or b.
            const juce::String letters ("c d ef g a b");
            const auto pos = letters.indexOfChar (t[0]);
            if (pos < 0 || t[0] == ' ')
                return -1;
            auto i = 1, semis = pos;
            if (i < t.length() && t[i] == '#') { ++semis; ++i; }
            else if (i < t.length() && t[i] == 'b') { --semis; ++i; }
            const auto octave = t.substring (i).getIntValue();
            return juce::jlimit (0, 127, (octave + 1) * 12 + semis);
        }

        std::shared_ptr<Program> load (const juce::File& file, const std::function<bool (double)>& progress)
        {
            auto program = std::make_shared<Program>();
            program->name = file.getFileNameWithoutExtension();
            program->source = file;
            std::map<juce::String, juce::String> defines;
            const auto parsed = parse (preprocess (file, defines, 0, program->problems));
            if (parsed.empty() && program->problems.isEmpty())
                program->problems.add ("no regions in " + file.getFileName());

            std::map<juce::String, int> unread;   // "opcode" or "opcode=value" -> regions
            std::map<juce::String, std::shared_ptr<const Sample>> loaded;   // path -> sample (each file once)
            struct Pending { Region region; juce::String path; float crossfadeSeconds = 0.0f; };
            std::vector<Pending> pending;                                  // regions, their files' paths, loop crossfades
            auto skippedRelease = 0, skippedGenerators = 0;

            for (const auto& op : parsed)
            {
                for (const auto& [k, v] : op)
                    if (k == "loop_mode" || k == "loopmode")
                    {
                        const auto mode = v.toLowerCase();
                        if (mode != "one_shot" && mode != "no_loop" && mode != "loop_continuous" && mode != "loop_sustain")
                            ++unread[k + "=" + v];
                    }
                    else if (! readOpcodes().count (k) && ! doesNothingToSound (k))
                        ++unread[k];
                // The <control> in force here.
                const auto controlValue = [&op] (const char* name, const juce::String& otherwise = {})
                {
                    const auto it = op.find (juce::String ("control.") + name);
                    return it != op.end() ? it->second : otherwise;
                };
                const auto defaultPath = controlValue ("default_path").replaceCharacter ('\\', '/');
                const auto keyShift = controlValue ("note_offset", "0").getIntValue() + 12 * controlValue ("octave_offset", "0").getIntValue();
                const auto keyOf = [keyShift] (const juce::String& text)
                {
                    const auto k = noteNumber (text);
                    return k < 0 ? -1 : k + keyShift;
                };
                const auto* sampleName = find (op, "sample");
                if (sampleName == nullptr || sampleName->isEmpty())
                    continue;
                if (sampleName->startsWithChar ('*'))
                {
                    ++skippedGenerators;   // *sine, *silence...: SFZ's built-in sounds, not read
                    continue;
                }
                if (const auto* trigger = find (op, "trigger"); trigger != nullptr && trigger->toLowerCase() != "attack")
                {
                    ++skippedRelease;      // release / first / legato samples (plan stage 2)
                    continue;
                }

                Region r;
                if (const auto* key = find (op, "key"); key != nullptr && keyOf (*key) >= 0)
                    r.loKey = r.hiKey = r.keyCenter = keyOf (*key);
                if (const auto* lo = find (op, "lokey"); lo != nullptr && keyOf (*lo) >= 0) r.loKey = keyOf (*lo);
                if (const auto* hi = find (op, "hikey"); hi != nullptr && keyOf (*hi) >= 0) r.hiKey = keyOf (*hi);
                if (const auto* centre = find (op, "pitch_keycenter"); centre != nullptr && keyOf (*centre) >= 0) r.keyCenter = keyOf (*centre);
                r.loKey = juce::jlimit (0, 127, r.loKey);
                r.hiKey = juce::jlimit (0, 127, r.hiKey);
                r.loVel = juce::jlimit (0, 127, (int) number (op, "lovel", 1.0f));
                r.hiVel = juce::jlimit (0, 127, (int) number (op, "hivel", 127.0f));
                r.loChan = juce::jlimit (1, 16, (int) number (op, "lochan", 1.0f));
                r.hiChan = juce::jlimit (1, 16, (int) number (op, "hichan", 16.0f));
                r.loRand = number (op, "lorand", 0.0f);
                r.hiRand = number (op, "hirand", 1.0f);
                r.seqLength = juce::jmax (1, (int) number (op, "seq_length", 1.0f));
                r.seqPosition = juce::jlimit (1, r.seqLength, (int) number (op, "seq_position", 1.0f));
                r.keytrack = number (op, "pitch_keytrack", 100.0f);
                r.tuneCents = number (op, "tune", number (op, "pitch", 0.0f)) + 100.0f * number (op, "transpose", 0.0f);
                r.gain = juce::Decibels::decibelsToGain (number (op, "volume", number (op, "gain", 0.0f)), -144.0f)
                       * juce::jlimit (0.0f, 1.0f, number (op, "amplitude", 100.0f) / 100.0f);
                r.pan = juce::jlimit (-1.0f, 1.0f, number (op, "pan", 0.0f) / 100.0f);
                r.velTrack = juce::jlimit (0.0f, 1.0f, number (op, "amp_veltrack", 100.0f) / 100.0f);
                r.offset = juce::jmax<juce::int64> (0, (juce::int64) number (op, "offset", 0.0f));
                if (const auto* end = find (op, "end"))
                    r.end = juce::jmax<juce::int64> (0, end->getLargeIntValue());
                r.delay = juce::jmax (0.0f, number (op, "ampeg_delay", 0.0f));
                r.attack = juce::jmax (0.0f, number (op, "ampeg_attack", 0.0f));
                r.hold = juce::jmax (0.0f, number (op, "ampeg_hold", 0.0f));
                r.decay = juce::jmax (0.0f, number (op, "ampeg_decay", 0.0f));
                r.sustain = juce::jlimit (0.0f, 1.0f, number (op, "ampeg_sustain", 100.0f) / 100.0f);
                r.release = juce::jmax (0.0f, number (op, "ampeg_release", 0.001f));
                const auto* mode = find (op, "loop_mode");
                if (mode == nullptr)
                    mode = find (op, "loopmode");
                const auto modeName = mode != nullptr ? mode->toLowerCase() : juce::String();
                r.oneShot = modeName == "one_shot";
                const auto* loopStart = find (op, "loop_start") != nullptr ? find (op, "loop_start") : find (op, "loopstart");
                const auto* loopEnd = find (op, "loop_end") != nullptr ? find (op, "loop_end") : find (op, "loopend");
                if ((modeName == "loop_continuous" || modeName == "loop_sustain") && loopStart != nullptr && loopEnd != nullptr
                    && loopEnd->getLargeIntValue() > loopStart->getLargeIntValue())
                {
                    r.loop = modeName == "loop_continuous" ? Region::Loop::continuous : Region::Loop::sustain;
                    r.loopStart = juce::jmax<juce::int64> (0, loopStart->getLargeIntValue());
                    r.loopEnd = loopEnd->getLargeIntValue();
                    r.loopCrossfade = -1;   // seconds read below, frames once the sample's rate is known
                }
                // Keyswitches: the program's switching keys and default; this region's switch.
                if (const auto* lo = find (op, "sw_lokey"); lo != nullptr && keyOf (*lo) >= 0)
                {
                    const auto hi = find (op, "sw_hikey") != nullptr ? keyOf (*find (op, "sw_hikey")) : keyOf (*lo);
                    program->swLoKey = program->swLoKey < 0 ? keyOf (*lo) : juce::jmin (program->swLoKey, keyOf (*lo));
                    program->swHiKey = juce::jmax (program->swHiKey, hi);
                }
                if (const auto* d = find (op, "sw_default"); d != nullptr && keyOf (*d) >= 0 && program->swDefault < 0)
                    program->swDefault = keyOf (*d);
                if (const auto* last = find (op, "sw_last"); last != nullptr && keyOf (*last) >= 0)
                    r.swLoLast = r.swHiLast = keyOf (*last);
                else if (const auto* lolast = find (op, "sw_lolast"); lolast != nullptr && keyOf (*lolast) >= 0)
                {
                    r.swLoLast = keyOf (*lolast);
                    const auto* hilast = find (op, "sw_hilast");
                    r.swHiLast = hilast != nullptr && keyOf (*hilast) >= 0 ? keyOf (*hilast) : r.swLoLast;
                }
                if (r.swLoLast >= 0)
                {
                    auto known = false;
                    for (const auto& a : program->articulations)
                        known = known || (a.lo == r.swLoLast && a.hi == r.swHiLast);
                    if (! known)
                    {
                        const auto* label = find (op, "sw_label");
                        program->articulations.push_back ({ r.swLoLast, r.swHiLast,
                                                            label != nullptr && label->isNotEmpty() ? label->trim()
                                                                : "Keyswitch " + juce::MidiMessage::getMidiNoteName (r.swLoLast, true, true, 4) });
                    }
                }
                const auto crossfadeSeconds = juce::jmax (0.0f, number (op, "loop_crossfade", 0.0f));
                r.bendUp = number (op, "bend_up", 200.0f);
                r.bendDown = number (op, "bend_down", -200.0f);
                if (r.loKey > r.hiKey || r.loVel > r.hiVel)
                    continue;

                const auto path = (defaultPath + *sampleName).replaceCharacter ('\\', '/');
                pending.push_back ({ std::move (r), path, crossfadeSeconds });
            }

            // The audio: each file once, in the order the regions name them.
            std::vector<juce::String> files;
            for (const auto& p : pending)
                if (loaded.emplace (p.path, nullptr).second)
                    files.push_back (p.path);
            for (size_t f = 0; f < files.size(); ++f)
            {
                if (progress && ! progress ((double) f / (double) files.size()))
                {
                    program->problems.add ("stopped: " + juce::String ((int) f) + " of " + juce::String ((int) files.size()) + " files loaded");
                    break;
                }
                const auto audio = file.getParentDirectory().getChildFile (files[f]);
                auto sample = sampleFiles::load (audio);
                if (sample == nullptr)
                    program->problems.add ((audio.existsAsFile() ? "can't read: " : "missing: ") + files[f]);
                else
                {
                    program->bytes += sample->bytes();
                    ++program->numFiles;
                }
                loaded[files[f]] = std::move (sample);
            }
            if (progress)
                progress (1.0);

            for (auto& p : pending)
            {
                auto& r = p.region;
                if (auto sample = loaded[p.path]; sample != nullptr && r.offset < sample->length() - 1)
                {
                    if (r.loop != Region::Loop::none)
                    {
                        // A loop past the file's end, or too short to play, isn't one.
                        r.loopEnd = juce::jmin (r.loopEnd, (juce::int64) sample->length() - 1);
                        if (r.loopEnd - r.loopStart < 2)
                            r.loop = Region::Loop::none;
                        r.loopCrossfade = (int) juce::jlimit<juce::int64> (0, (r.loopEnd - r.loopStart) / 2,
                                                                          (juce::int64) (p.crossfadeSeconds * sample->sampleRate));
                    }
                    r.sample = std::move (sample);
                    program->regions.push_back (std::move (r));
                }
            }
            if (skippedRelease > 0)
                program->unread.add ("release / legato samples (" + juce::String (skippedRelease) + ")");
            if (skippedGenerators > 0)
                program->unread.add ("built-in sounds such as *sine (" + juce::String (skippedGenerators) + ")");
            for (const auto& [opcode, count] : unread)
                program->unread.add (opcode + " (" + juce::String (count) + ")");
            prepareRegions (*program);
            program->index();
            return program;
        }
    }
}
