#include "dsp/sampler/Section.h"

#include <set>

namespace sampler::section
{
    namespace
    {
        const juce::Identifier sectionTag { "Section" }, partTag { "Part" };

        // The kinds in the order a section lists them; others after, in the order the parts give them.
        const juce::StringArray& usualKinds()
        {
            static const juce::StringArray kinds { "sustain", "sustain non-vibrato", "tremolo", "legato", "marcato",
                                                   "spiccato", "staccato", "pizzicato" };
            return kinds;
        }

        // The label without a leading key name ("C#2 Tremolo" -> "Tremolo").
        juce::String withoutKeyName (const juce::String& label)
        {
            const auto trimmed = label.trim();
            const auto first = trimmed.upToFirstOccurrenceOf (" ", false, false).toLowerCase();
            auto isKeyName = first.length() >= 2 && first[0] >= 'a' && first[0] <= 'g';
            for (int i = 1; isKeyName && i < first.length(); ++i)
                isKeyName = (i == 1 && (first[i] == '#' || first[i] == 'b')) || first[i] == '-' || juce::CharacterFunctions::isDigit (first[i]);
            isKeyName = isKeyName && juce::CharacterFunctions::isDigit (first.getLastCharacter());
            return isKeyName ? trimmed.fromFirstOccurrenceOf (" ", false, false).trim() : trimmed;
        }
    }

    juce::String kindOf (const juce::String& label)
    {
        const auto l = withoutKeyName (label).toLowerCase();
        if (l.contains ("pizz")) return "pizzicato";
        if (l.contains ("spic")) return "spiccato";
        if (l.contains ("stac")) return "staccato";
        if (l.contains ("trem")) return "tremolo";
        if (l.contains ("legato")) return "legato";
        if (l.contains ("marc")) return "marcato";
        if (l.contains ("sus") || l.contains ("arco") || l.contains ("vib") || l == "long")
        {
            const auto plain = " " + l.replaceCharacter ('-', ' ').replaceCharacter ('_', ' ') + " ";
            const auto nonVibrato = plain.contains ("non vib") || plain.contains ("nonvib") || plain.contains (" nv ")
                                 || plain.contains ("no vib") || plain.contains ("senza vib");
            return nonVibrato ? "sustain non-vibrato" : "sustain";
        }
        return l;
    }

    juce::String nameOfKind (const juce::String& kind, const juce::String& label)
    {
        if (kind == "sustain non-vibrato")
            return "Sustain Non-Vibrato";
        if (usualKinds().contains (kind))
            return kind.substring (0, 1).toUpperCase() + kind.substring (1);
        return withoutKeyName (label);
    }

    juce::String Section::toText() const
    {
        juce::XmlElement xml (sectionTag);
        for (const auto& p : parts)
        {
            auto* e = xml.createNewChildElement (partTag);
            e->setAttribute ("file", p.file);
            e->setAttribute ("low", p.low);
            e->setAttribute ("high", p.high);
        }
        return xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    }

    bool isSection (const juce::String& text)
    {
        return text.trimStart().startsWith ("<" + sectionTag.toString());
    }

    std::optional<Section> fromText (const juce::String& text)
    {
        if (! isSection (text))
            return std::nullopt;
        const auto xml = juce::parseXML (text);
        if (xml == nullptr || ! xml->hasTagName (sectionTag))
            return std::nullopt;
        Section section;
        for (const auto* e : xml->getChildWithTagNameIterator (partTag))
        {
            Part p;
            p.file = e->getStringAttribute ("file");
            p.low = juce::jlimit (0, 127, e->getIntAttribute ("low", 0));
            p.high = juce::jlimit (0, 127, e->getIntAttribute ("high", 127));
            if (p.file.isNotEmpty() && p.low <= p.high)
                section.parts.push_back (p);
        }
        if (section.parts.empty())
            return std::nullopt;
        return section;
    }

    juce::String nameOf (const Part& p)
    {
        return juce::File (p.file).getFileNameWithoutExtension();
    }

    juce::String describe (const juce::String& instrument)
    {
        if (const auto section = fromText (instrument))
        {
            juce::StringArray names;
            for (const auto& p : section->parts)
                names.add (nameOf (p));
            return names.joinIntoString (" + ");
        }
        if (isSection (instrument))
            return "a section";
        return juce::File::isAbsolutePath (instrument) ? juce::File (instrument).getFileNameWithoutExtension() : instrument;
    }

    juce::File folderOf (const juce::String& instrument)
    {
        if (const auto section = fromText (instrument))
            return juce::File (section->parts.front().file).getParentDirectory();
        return juce::File::isAbsolutePath (instrument) ? juce::File (instrument).getParentDirectory() : juce::File();
    }

    Section fromFiles (const juce::StringArray& files)
    {
        struct Range { juce::String file; int lowest, highest; };
        std::vector<Range> ranges;
        for (const auto& f : files)
        {
            const juce::File file (f);
            if (! file.existsAsFile() || ! file.hasFileExtension ("sfz"))
                continue;
            sfz::LoadOptions keysOnly;
            keysOnly.audio = false;
            const auto read = sfz::load (file, {}, keysOnly);
            if (read->lowestKey() >= 0)
                ranges.push_back ({ file.getFullPathName(), read->lowestKey(), read->highestKey() });
        }
        // 0.6.0: ordered by the middle of their ranges (a horn's recordings reach under a trombone's, but it plays
        // above it), each starting at its own lowest key unless that would leave the part beneath fewer than
        // `fewestKeys` (or start under it): then the two share their overlap, split at its middle. 7 is the violas'
        // share in VSCO 2 CE's strings, so their splits stay 0.4.0's; its brass (sustains): tuba 29-47, trombone 48-56,
        // horn 57-67, trumpet 68-84 (by lowest keys alone the tuba had 5 keys and the horn 1).
        constexpr int fewestKeys = 7;
        std::stable_sort (ranges.begin(), ranges.end(), [] (const Range& a, const Range& b)
        {
            const auto middleA = a.lowest + a.highest, middleB = b.lowest + b.highest;   // (twice the middles)
            return middleA != middleB ? middleA < middleB : a.lowest < b.lowest;
        });
        std::vector<int> starts;
        for (size_t i = 0; i < ranges.size(); ++i)
        {
            auto start = ranges[i].lowest;
            if (i > 0 && start < starts.back() + fewestKeys)
            {
                const auto from = juce::jmax (start, starts.back() + 1), to = juce::jmax (from, juce::jmin (ranges[i - 1].highest, ranges[i].highest));
                start = (from + to + 1) / 2;
            }
            starts.push_back (juce::jlimit (0, 127, i > 0 ? juce::jmax (start, starts.back() + 1) : start));
        }
        Section section;
        for (size_t i = 0; i < ranges.size(); ++i)
        {
            Part p;
            p.file = ranges[i].file;
            p.low = juce::jmin (starts[i], 127);
            p.high = juce::jlimit (p.low, 127, i + 1 < ranges.size() ? starts[i + 1] - 1 : ranges[i].highest);
            section.parts.push_back (p);
        }
        return section;
    }

    std::shared_ptr<Program> load (const Section& section, const std::function<bool (double)>& progress,
                                   const std::function<std::shared_ptr<const Sample> (const juce::String&)>& reuse)
    {
        auto result = std::make_shared<Program>();
        juce::StringArray names;
        for (const auto& p : section.parts)
            names.add (nameOf (p));
        result->name = names.joinIntoString (" + ");

        // Each part, only its keys.
        std::vector<std::shared_ptr<Program>> parts;
        const auto count = (int) section.parts.size();
        for (int i = 0; i < count; ++i)
        {
            const auto& part = section.parts[(size_t) i];
            const juce::File file (part.file);
            if (! file.existsAsFile())
            {
                result->problems.add ("missing: " + part.file);
                parts.push_back (nullptr);
                continue;
            }
            sfz::LoadOptions options;
            options.loKey = part.low;
            options.hiKey = part.high;
            options.reuse = reuse;
            auto stopped = false;
            auto made = sfz::load (file, [&] (double done)
            {
                stopped = progress && ! progress (((double) i + done) / (double) count);
                return ! stopped;
            }, options);
            for (const auto& problem : made->problems)
                result->problems.add (nameOf (part) + ": " + problem);
            for (const auto& u : made->unread)
                result->unread.add (nameOf (part) + ": " + u);
            parts.push_back (std::move (made));
            if (stopped)
                break;
        }

        // The articulations: every part's, matched by kind.
        juce::StringArray kinds, labels;
        for (const auto& p : parts)
            if (p != nullptr)
                for (const auto& a : p->articulations)
                    if (const auto kind = kindOf (a.label); ! kinds.contains (kind))
                    {
                        kinds.add (kind);
                        labels.add (a.label);
                    }
        std::vector<int> order ((size_t) kinds.size());
        for (int i = 0; i < kinds.size(); ++i)
            order[(size_t) i] = i;
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b)
        {
            const auto rank = [&] (int i) { const auto u = usualKinds().indexOf (kinds[i]); return u >= 0 ? u : 100; };
            return rank (a) < rank (b);
        });

        // Where its switch keys go: from the first C over the section's highest key; under its lowest if that passes C8.
        auto lowest = 127, highest = 0;
        for (const auto& p : parts)
            if (p != nullptr && p->lowestKey() >= 0)
            {
                lowest = juce::jmin (lowest, p->lowestKey());
                highest = juce::jmax (highest, p->highestKey());
            }
        const auto numSwitches = (int) order.size();
        auto base = (highest / 12 + 1) * 12;
        if (numSwitches > 0 && base + numSwitches - 1 > 108 && lowest - numSwitches >= 0)
            base = ((lowest - numSwitches) / 12) * 12;
        base = juce::jlimit (0, 128 - juce::jmax (1, numSwitches), base);
        std::map<juce::String, int> keyOfKind;
        for (int i = 0; i < numSwitches; ++i)
        {
            const auto k = base + i;
            const auto& kind = kinds[order[(size_t) i]];
            keyOfKind[kind] = k;
            result->articulations.push_back ({ k, k, juce::MidiMessage::getMidiNoteName (k, true, true, 4) + " "
                                                         + nameOfKind (kind, labels[order[(size_t) i]]) });
        }
        if (numSwitches > 0)
        {
            result->swLoKey = base;
            result->swHiKey = base + numSwitches - 1;
            result->swDefault = keyOfKind.count ("sustain") > 0 ? keyOfKind["sustain"] : base;
        }

        // The regions: each part's, under the section's switches.
        std::set<const Sample*> samples;
        for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex)
        {
            const auto& p = parts[partIndex];
            if (p == nullptr)
                continue;
            for (auto& r : p->regions)   // 0.8.0: each region knows its part (THE STAGE seats parts apart)
                r.part = (int) partIndex;
            // Which of the part's own switches plays under each of the section's (its own kind, else its nearest).
            std::vector<std::pair<int, int>> switchFor;   // (the part's switch key, the section's)
            for (const auto& [kind, key] : keyOfKind)
            {
                const Program::Articulation* chosen = nullptr;
                for (const auto& a : p->articulations)
                    if (kindOf (a.label) == kind && chosen == nullptr)
                        chosen = &a;
                if (chosen == nullptr && kind.startsWith ("sustain"))
                    for (const auto& a : p->articulations)
                        if (kindOf (a.label).startsWith ("sustain") && chosen == nullptr)
                            chosen = &a;
                if (chosen != nullptr)
                    switchFor.push_back ({ chosen->lo, key });
            }
            for (const auto& r : p->regions)
            {
                if (r.sample != nullptr)
                    samples.insert (r.sample.get());
                if (r.swLoLast < 0 || p->articulations.empty())
                {
                    result->regions.push_back (r);
                    result->regions.back().swLoLast = result->regions.back().swHiLast = -1;
                    continue;
                }
                for (const auto& [own, key] : switchFor)
                    if (own >= r.swLoLast && own <= r.swHiLast)
                    {
                        auto copy = r;
                        copy.swLoLast = copy.swHiLast = key;
                        result->regions.push_back (std::move (copy));
                    }
            }
        }
        for (const auto& p : parts)
            if (p != nullptr)
                for (const auto& r : p->regions)
                    if (r.sample != nullptr && samples.erase (r.sample.get()) > 0)
                    {
                        result->bytes += r.sample->bytes();
                        ++result->numFiles;
                    }
        result->index();
        return result;
    }
}
