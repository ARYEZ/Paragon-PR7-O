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

    int Program::velocityLayersAt (int key) const
    {
        if (key < 0 || key >= 128)
            return 0;
        std::set<std::pair<int, int>> ranges;
        for (const auto i : byKey[(size_t) key])
            ranges.insert ({ regions[(size_t) i].loVel, regions[(size_t) i].hiVel });
        return (int) ranges.size();
    }

    int Program::roundRobinsAt (int key) const
    {
        if (key < 0 || key >= 128)
            return 0;
        std::map<std::pair<int, int>, int> perRange;
        auto most = 0;
        for (const auto i : byKey[(size_t) key])
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

            // The headers and their opcodes, each region with everything it inherits; and <control>'s.
            std::vector<Opcodes> parse (const juce::String& text, Opcodes& control)
            {
                std::vector<Opcodes> regions;
                Opcodes global, master, group, current;
                juce::String header;
                const auto flush = [&]
                {
                    if (header == "region")
                    {
                        Opcodes r;
                        for (const auto* level : { &global, &master, &group, &current })
                            for (const auto& [k, v] : *level)
                                r[k] = v;
                        regions.push_back (std::move (r));
                    }
                    else if (header == "control")
                        for (const auto& [k, v] : current)
                            control[k] = v;
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
                    "bend_up", "bend_down", "trigger", "default_path", "note_offset", "octave_offset" };
                return names;
            }
            bool doesNothingToSound (const juce::String& opcode)
            {
                return opcode.endsWith ("_label") || opcode.startsWith ("label_cc") || opcode.startsWith ("hint_")
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
            Opcodes control;
            const auto parsed = parse (preprocess (file, defines, 0, program->problems), control);
            if (parsed.empty() && program->problems.isEmpty())
                program->problems.add ("no regions in " + file.getFileName());

            const auto defaultPath = control.count ("default_path") ? control["default_path"].replaceCharacter ('\\', '/') : juce::String();
            const auto keyShift = (int) number (control, "note_offset", 0.0f) + 12 * (int) number (control, "octave_offset", 0.0f);
            const auto keyOf = [keyShift] (const juce::String& text)
            {
                const auto k = noteNumber (text);
                return k < 0 ? -1 : k + keyShift;
            };

            std::map<juce::String, int> unread;   // "opcode" or "opcode=value" -> regions
            std::map<juce::String, std::shared_ptr<const Sample>> loaded;   // path -> sample (each file once)
            std::vector<std::pair<Region, juce::String>> pending;           // regions and their files' paths
            auto skippedRelease = 0, skippedGenerators = 0;

            for (const auto& op : parsed)
            {
                for (const auto& [k, v] : op)
                    if (k == "loop_mode")
                    {
                        if (v.toLowerCase() != "one_shot" && v.toLowerCase() != "no_loop")
                            ++unread[k + "=" + v];   // loops: plan stage 2
                    }
                    else if (! readOpcodes().count (k) && ! doesNothingToSound (k))
                        ++unread[k];
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
                if (const auto* mode = find (op, "loop_mode"))
                    r.oneShot = mode->toLowerCase() == "one_shot";
                r.bendUp = number (op, "bend_up", 200.0f);
                r.bendDown = number (op, "bend_down", -200.0f);
                if (r.loKey > r.hiKey || r.loVel > r.hiVel)
                    continue;

                const auto path = (defaultPath + *sampleName).replaceCharacter ('\\', '/');
                pending.emplace_back (std::move (r), path);
            }

            // The audio: each file once, in the order the regions name them.
            std::vector<juce::String> files;
            for (const auto& [r, path] : pending)
                if (loaded.emplace (path, nullptr).second)
                    files.push_back (path);
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

            for (auto& [r, path] : pending)
                if (auto sample = loaded[path]; sample != nullptr && r.offset < sample->length() - 1)
                {
                    r.sample = std::move (sample);
                    program->regions.push_back (std::move (r));
                }
            if (skippedRelease > 0)
                program->unread.add ("release / legato samples (" + juce::String (skippedRelease) + ")");
            if (skippedGenerators > 0)
                program->unread.add ("built-in sounds such as *sine (" + juce::String (skippedGenerators) + ")");
            for (const auto& [opcode, count] : unread)
                program->unread.add (opcode + " (" + juce::String (count) + ")");
            program->index();
            return program;
        }
    }
}
