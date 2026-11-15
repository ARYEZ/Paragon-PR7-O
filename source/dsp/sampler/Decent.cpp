#include "dsp/sampler/Decent.h"

#include <array>
#include <cmath>
#include <functional>
#include <set>

namespace sampler::decent
{
    bool isPreset (const juce::File& f) { return f.hasFileExtension ("dspreset"); }

    namespace
    {
        // A <binding>'s translation of an input `x` (from a source running xMin..xMax: a knob's range, a controller's
        // 0..127, a velocity's 0..1) into the value it sets.
        struct Binding
        {
            juce::String type, level, parameter, identifier, translation;
            int position = -1, midiElement = -1;
            float outMin = 0.0f, outMax = 1.0f;
            juce::String fixed;
            std::vector<std::pair<float, float>> table;

            static Binding of (const juce::XmlElement& e)
            {
                Binding b;
                b.type = e.getStringAttribute ("type").trim().toLowerCase();
                b.level = e.getStringAttribute ("level").trim().toLowerCase();
                b.parameter = e.getStringAttribute ("parameter").trim().toUpperCase();
                b.identifier = e.getStringAttribute ("identifier").trim();
                b.translation = e.getStringAttribute ("translation").trim().toLowerCase();
                b.position = e.getIntAttribute ("position", -1);
                b.midiElement = e.getIntAttribute ("midiElementIndex", -1);
                b.outMin = (float) e.getStringAttribute ("translationOutputMin", "0").trim().getDoubleValue();
                b.outMax = (float) e.getStringAttribute ("translationOutputMax", "1").trim().getDoubleValue();
                b.fixed = e.getStringAttribute ("translationValue").trim();
                for (const auto& point : juce::StringArray::fromTokens (e.getStringAttribute ("translationTable"), ";", ""))
                    if (point.contains (","))
                        b.table.push_back ({ (float) point.upToFirstOccurrenceOf (",", false, false).trim().getDoubleValue(),
                                             (float) point.fromFirstOccurrenceOf (",", false, false).trim().getDoubleValue() });
                return b;
            }

            float operator() (float x, float xMin, float xMax, bool rawWhenUntranslated) const
            {
                if (translation == "fixed_value")
                    return fixed == "true" ? 1.0f : fixed == "false" ? 0.0f : (float) fixed.getDoubleValue();
                if (translation == "table" && ! table.empty())
                {
                    if (x <= table.front().first)
                        return table.front().second;
                    for (size_t i = 1; i < table.size(); ++i)
                        if (x <= table[i].first)
                        {
                            const auto& [x0, y0] = table[i - 1];
                            const auto& [x1, y1] = table[i];
                            return x1 > x0 ? y0 + (y1 - y0) * (x - x0) / (x1 - x0) : y1;
                        }
                    return table.back().second;
                }
                const auto t = xMax > xMin ? juce::jlimit (0.0f, 1.0f, (x - xMin) / (xMax - xMin)) : 0.0f;
                if (translation == "linear" || ! rawWhenUntranslated)
                    return outMin + (outMax - outMin) * t;
                return x;
            }
        };

        // A DS volume: "-6dB" (or "-6 dB") in decibels, a plain number as a linear gain.
        float gainOf (const juce::String& text)
        {
            const auto t = text.trim();
            if (t.endsWithIgnoreCase ("db"))
                return std::pow (10.0f, (float) t.dropLastCharacters (2).trim().getDoubleValue() / 20.0f);
            return juce::jmax (0.0f, (float) t.getDoubleValue());
        }

        bool isTrue (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();
            return t == "true" || t == "1" || t == "yes";
        }

        int noteOf (const juce::String& text, int otherwise)
        {
            const auto t = text.trim().toLowerCase();
            if (t.isEmpty())
                return otherwise;
            if (t.containsOnly ("0123456789"))
                return juce::jlimit (0, 127, t.getIntValue());
            // A note name ("c#3", "eb2"): DS counts middle C as C3 (60), as most DAWs it's used in.
            static const juce::String letters ("c d ef g a b");
            const auto semitone = letters.indexOfChar (t[0]);
            if (semitone < 0)
                return otherwise;
            auto n = semitone, i = 1;
            if (t[1] == '#') { ++n; ++i; }
            else if (t[1] == 'b' && t.length() > 2) { --n; ++i; }
            return juce::jlimit (0, 127, (t.substring (i).getIntValue() + 2) * 12 + n);
        }

        // What a <sample> takes from its <group> and the <groups> above it: its own attribute, else the group's, else
        // the groups'.
        struct Chain
        {
            const juce::XmlElement* sample = nullptr;
            const juce::XmlElement* group = nullptr;
            const std::map<juce::String, juce::String>* top = nullptr;   // <groups>, with the screen's settings applied

            juce::String get (const char* name, const juce::String& otherwise = {}) const
            {
                if (sample != nullptr && sample->hasAttribute (name)) return sample->getStringAttribute (name);
                if (group != nullptr && group->hasAttribute (name)) return group->getStringAttribute (name);
                if (const auto it = top->find (name); it != top->end()) return it->second;
                return otherwise;
            }
            bool has (const char* name) const
            {
                return (sample != nullptr && sample->hasAttribute (name)) || (group != nullptr && group->hasAttribute (name)) || top->count (name) > 0;
            }
        };

        using Curve = std::array<float, 128>;
    }

    std::vector<Opcodes> read (const juce::File& preset, juce::StringArray& problems, std::map<juce::String, int>& unread)
    {
        std::vector<Opcodes> regions;
        const auto xml = juce::XmlDocument::parse (preset);
        if (xml == nullptr || ! xml->hasTagName ("DecentSampler"))
        {
            problems.add ("not a Decent Sampler preset: " + preset.getFileName());
            return regions;
        }
        const auto* groups = xml->getChildByName ("groups");
        if (groups == nullptr)
        {
            problems.add ("no <groups> in " + preset.getFileName());
            return regions;
        }

        // The instrument's settings (<groups>' attributes), which its screen and menus may set; the tags' volumes.
        std::map<juce::String, juce::String> top;
        for (int i = 0; i < groups->getNumAttributes(); ++i)
            top[groups->getAttributeName (i)] = groups->getAttributeValue (i);
        std::map<juce::String, float> tagVolume;
        if (const auto* tags = xml->getChildByName ("tags"))
            for (const auto* t : tags->getChildWithTagNameIterator ("tag"))
                tagVolume[t->getStringAttribute ("name").trim()] = gainOf (t->getStringAttribute ("volume", "1"));
        auto instrumentVolume = 1.0f;
        std::set<int> disabledMidi;   // <midi> elements a menu switched off (midiElementIndex)

        // A binding set to a value once, as the instrument opens (a knob's or a menu's).
        const auto applyOnce = [&] (const Binding& b, float value)
        {
            if (b.type == "amp" && b.level == "instrument")
            {
                static const std::map<juce::String, const char*> envelope { { "ENV_ATTACK", "attack" }, { "ENV_DECAY", "decay" },
                                                                            { "ENV_SUSTAIN", "sustain" }, { "ENV_RELEASE", "release" },
                                                                            { "AMP_VEL_TRACK", "ampVelTrack" } };
                if (const auto it = envelope.find (b.parameter); it != envelope.end())
                    top[it->second] = juce::String (value);
                else if (b.parameter == "AMP_VOLUME")
                    instrumentVolume = juce::jmax (0.0f, value);
            }
            else if (b.type == "amp" && b.level == "tag" && b.parameter == "TAG_VOLUME" && b.identifier.isNotEmpty())
                tagVolume[b.identifier] = juce::jmax (0.0f, value);
            else if (b.type == "note" && b.level == "midi" && b.parameter == "ENABLED" && b.midiElement >= 0)
            {
                if (value > 0.5f) disabledMidi.erase (b.midiElement);
                else disabledMidi.insert (b.midiElement);
            }
            // (its effects' settings, the glide time and the attack's curve: not played by PR7-O)
        };

        // The screen: each element of its first tab by position (a controller's binding names a knob by it).
        struct Knob { float value = 0.0f, min = 0.0f, max = 1.0f; juce::String label; std::vector<Binding> bindings; bool isKnob = false; };
        std::vector<Knob> screen;
        if (const auto* ui = xml->getChildByName ("ui"))
            if (const auto* tab = ui->getChildByName ("tab"))
                for (const auto* e : tab->getChildIterator())
                {
                    Knob k;
                    const auto tag = e->getTagName();
                    if (tag == "control" || tag == "labeled-knob")
                    {
                        k.isKnob = true;
                        k.min = (float) e->getDoubleAttribute ("minValue", 0.0);
                        k.max = (float) e->getDoubleAttribute ("maxValue", 1.0);
                        k.value = (float) e->getDoubleAttribute ("value", e->getDoubleAttribute ("defaultValue", k.min));
                        k.label = e->getStringAttribute ("parameterName", e->getStringAttribute ("label"));
                        for (const auto* b : e->getChildWithTagNameIterator ("binding"))
                            k.bindings.push_back (Binding::of (*b));
                    }
                    else if (tag == "menu")
                    {
                        // Its chosen option's bindings (value: counted from 1).
                        auto index = e->getIntAttribute ("value", 1) - 1, n = 0;
                        for (const auto* option : e->getChildWithTagNameIterator ("option"))
                            if (n++ == index)
                                for (const auto* b : option->getChildWithTagNameIterator ("binding"))
                                {
                                    const auto binding = Binding::of (*b);
                                    applyOnce (binding, binding (0.0f, 0.0f, 1.0f, true));
                                }
                    }
                    else if (tag == "button")
                        ++unread["screen buttons"];
                    screen.push_back (std::move (k));
                }

        // The level curves: controllers bound to the instrument's or a tag's volume, directly or through a knob.
        std::map<int, Curve> instrumentCurves;                 // cc -> gain at each value
        std::map<juce::String, std::map<int, Curve>> tagCurves;
        std::map<int, float> startCC;                          // cc -> its value as the instrument opens
        std::map<int, juce::String> ccLabels;
        std::set<size_t> drivenKnobs;
        const auto multiply = [] (std::map<int, Curve>& curves, int cc, const Curve& c)
        {
            auto [it, fresh] = curves.try_emplace (cc);
            if (fresh)
                it->second.fill (1.0f);
            for (size_t v = 0; v < 128; ++v)
                it->second[v] *= c[v];
        };
        const auto curveFor = [&] (int cc, const Binding& target, const std::function<float (int)>& sourceValue, float sMin, float sMax,
                                   bool rawWhenUntranslated)
        {
            if (target.type != "amp")
                return false;
            Curve c;
            for (int v = 0; v < 128; ++v)
                c[(size_t) v] = juce::jmax (0.0f, target (sourceValue (v), sMin, sMax, rawWhenUntranslated));
            if (target.level == "instrument" && target.parameter == "AMP_VOLUME")
                multiply (instrumentCurves, cc, c);
            else if (target.level == "tag" && target.parameter == "TAG_VOLUME" && target.identifier.isNotEmpty())
                multiply (tagCurves[target.identifier], cc, c);
            else
                return false;
            return true;
        };
        std::vector<std::pair<std::vector<std::pair<float, float>>, int>> velocityAttack;   // (points, midi element)
        if (const auto* midi = xml->getChildByName ("midi"))
        {
            int element = 0;
            for (const auto* e : midi->getChildIterator())
            {
                const auto index = element++;
                if (e->hasTagName ("cc"))
                {
                    const auto cc = juce::jlimit (0, 127, e->getIntAttribute ("number", 1));
                    for (const auto* be : e->getChildWithTagNameIterator ("binding"))
                    {
                        const auto b = Binding::of (*be);
                        if (b.type == "control" && b.level == "ui" && b.position >= 0 && b.position < (int) screen.size()
                            && screen[(size_t) b.position].isKnob)
                        {
                            // The controller moves a knob: the knob's own bindings follow it.
                            auto& knob = screen[(size_t) b.position];
                            const auto knobValue = [&b] (int v) { return b ((float) v, 0.0f, 127.0f, false); };
                            auto any = false;
                            for (const auto& target : knob.bindings)
                                any = curveFor (cc, target, knobValue, knob.min, knob.max, true) || any;
                            if (any)
                            {
                                drivenKnobs.insert ((size_t) b.position);
                                // Where the controller starts: the value that puts the knob where it opens.
                                auto best = 127;
                                auto bestError = 1.0e9f;
                                for (int v = 0; v < 128; ++v)
                                    if (const auto error = std::abs (knobValue (v) - knob.value); error < bestError)
                                    {
                                        best = v;
                                        bestError = error;
                                    }
                                startCC[cc] = (float) best;
                                ccLabels[cc] = knob.label;
                            }
                        }
                        else if (curveFor (cc, b, [] (int v) { return (float) v; }, 0.0f, 127.0f, false))
                        {
                            if (! startCC.count (cc))
                                startCC[cc] = -1.0f;   // (chosen below, once the starting volumes are known)
                        }
                        else if (b.type != "effect")
                            ++unread["controller binding " + b.parameter];
                    }
                }
                else if (e->hasTagName ("note"))
                {
                    for (const auto* be : e->getChildWithTagNameIterator ("binding"))
                    {
                        const auto b = Binding::of (*be);
                        if (b.type == "amp" && b.level == "instrument" && b.parameter == "ENV_ATTACK")
                        {
                            std::vector<std::pair<float, float>> points;
                            for (int i = 0; i <= 32; ++i)
                            {
                                const auto x = (float) i / 32.0f;
                                points.push_back ({ x, juce::jmax (0.0f, b (x, 0.0f, 1.0f, false)) });
                            }
                            velocityAttack.push_back ({ std::move (points), index });
                        }
                        else if (b.parameter != "ENV_ATTACK_CURVE")
                            ++unread["velocity binding " + b.parameter];
                    }
                }
            }
        }

        // The knobs not moved by a controller: their bindings set once, at their starting values.
        for (size_t i = 0; i < screen.size(); ++i)
            if (screen[i].isKnob && ! drivenKnobs.count (i))
                for (const auto& b : screen[i].bindings)
                    applyOnce (b, b (screen[i].value, screen[i].min, screen[i].max, true));

        // A controller bound straight to a volume, with no knob to say where it starts: where its curves come
        // closest to the volumes as they'd be before it moved (all at full, unless the file says otherwise).
        for (auto& [cc, start] : startCC)
            if (start < 0.0f)
            {
                auto best = 127;
                auto bestError = 1.0e9f;
                for (int v = 127; v >= 0; --v)
                {
                    auto error = 0.0f;
                    if (const auto it = instrumentCurves.find (cc); it != instrumentCurves.end())
                        error += std::abs (it->second[(size_t) v] - instrumentVolume);
                    for (const auto& [tag, curves] : tagCurves)
                        if (const auto it = curves.find (cc); it != curves.end())
                            error += std::abs (it->second[(size_t) v] - (tagVolume.count (tag) ? tagVolume[tag] : 1.0f));
                    if (error < bestError - 1.0e-6f)
                    {
                        best = v;
                        bestError = error;
                    }
                }
                start = (float) best;
            }

        // The attack by velocity, unless a menu switched its <note> off.
        const std::vector<std::pair<float, float>>* attackByVelocity = nullptr;
        for (const auto& [points, element] : velocityAttack)
            if (! disabledMidi.count (element))
                attackByVelocity = &points;

        // Round robins: how many places each kind of sequence has.
        auto randomPositions = 1, robinPositions = 1;
        for (const auto* group : groups->getChildWithTagNameIterator ("group"))
            for (const auto* s : group->getChildWithTagNameIterator ("sample"))
            {
                const Chain chain { s, group, &top };
                const auto mode = chain.get ("seqMode", "always").toLowerCase();
                const auto position = chain.get ("seqPosition", "0").getIntValue();
                if (mode == "random" || mode == "true_random")
                    randomPositions = juce::jmax (randomPositions, position);
                else if (mode == "round_robin")
                    robinPositions = juce::jmax (robinPositions, chain.get ("seqLength", juce::String (position)).getIntValue(), position);
            }

        static const std::set<juce::String> known {
            "path", "rootNote", "loNote", "hiNote", "loVel", "hiVel", "start", "end", "volume", "tuning", "pan", "pitchKeyTrack",
            "loopStart", "loopEnd", "loopEnabled", "loopCrossfade", "loopCrossfadeMode", "trigger", "tags", "seqMode", "seqPosition",
            "seqLength", "attack", "decay", "sustain", "release", "ampVelTrack", "name", "enabled", "glideTime", "attackCurve",
            "decayCurve", "releaseCurve", "legatoTime", "groupName", "silencingMode" };
        const auto noteUnknown = [&] (const juce::XmlElement& e)
        {
            for (int i = 0; i < e.getNumAttributes(); ++i)
                if (! known.count (e.getAttributeName (i)))
                    ++unread[e.getAttributeName (i)];
        };
        for (const auto& [name, value] : top)
            if (! known.count (name))
                ++unread[name];

        int groupIndex = 0;
        for (const auto* group : groups->getChildWithTagNameIterator ("group"))
        {
            ++groupIndex;
            noteUnknown (*group);
            if (group->hasAttribute ("enabled") && ! isTrue (group->getStringAttribute ("enabled")))
                continue;
            juce::StringArray groupTags = juce::StringArray::fromTokens (group->getStringAttribute ("tags"), ",", "");
            groupTags.trim();
            for (const auto* s : group->getChildWithTagNameIterator ("sample"))
            {
                noteUnknown (*s);
                const Chain chain { s, group, &top };
                const auto path = s->getStringAttribute ("path").trim().replaceCharacter ('\\', '/');
                if (path.isEmpty())
                    continue;
                Opcodes op;
                op["sample"] = path;
                const auto root = noteOf (chain.get ("rootNote"), 60);
                op["pitch_keycenter"] = juce::String (root);
                op["lokey"] = juce::String (noteOf (chain.get ("loNote"), root));
                op["hikey"] = juce::String (noteOf (chain.get ("hiNote"), root));
                op["lovel"] = juce::String (juce::jlimit (0, 127, chain.get ("loVel", "0").getIntValue()));
                op["hivel"] = juce::String (juce::jlimit (0, 127, chain.get ("hiVel", "127").getIntValue()));
                if (s->hasAttribute ("start")) op["offset"] = s->getStringAttribute ("start").trim();
                if (s->hasAttribute ("end")) op["end"] = s->getStringAttribute ("end").trim();

                // Level: the sample's, its group's and the instrument's volumes, and its tags' (all as they open,
                // where no controller moves them; those that move are curves below).
                auto gain = gainOf (s->getStringAttribute ("volume", "1")) * gainOf (group->getStringAttribute ("volume", "1"))
                          * (top.count ("volume") ? gainOf (top["volume"]) : 1.0f);
                juce::StringArray tags = groupTags;
                for (const auto& t : juce::StringArray::fromTokens (s->getStringAttribute ("tags"), ",", ""))
                    tags.addIfNotAlreadyThere (t.trim());
                std::map<int, Curve> curves;
                if (instrumentCurves.empty())
                    gain *= instrumentVolume;
                else
                    for (const auto& [cc, c] : instrumentCurves)
                        multiply (curves, cc, c);
                for (const auto& tag : tags)
                {
                    if (const auto it = tagCurves.find (tag); it != tagCurves.end())
                        for (const auto& [cc, c] : it->second)
                            multiply (curves, cc, c);
                    else if (const auto v = tagVolume.find (tag); v != tagVolume.end())
                        gain *= v->second;
                }
                op["volume"] = juce::String (gain > 0.0f ? juce::jmax (-144.0f, 20.0f * std::log10 (gain)) : -144.0f);
                for (const auto& [cc, c] : curves)
                {
                    juce::StringArray values;
                    for (const auto v : c)
                        values.add (juce::String (v, 5));
                    op["pr7o_cccurve" + juce::String (cc)] = values.joinIntoString (",");
                }
                for (const auto& [cc, start] : startCC)
                    op["set_cc" + juce::String (cc)] = juce::String (juce::roundToInt (start));
                for (const auto& [cc, label] : ccLabels)
                    op["label_cc" + juce::String (cc)] = label;

                // Pitch, pan.
                const auto tuning = (float) s->getDoubleAttribute ("tuning", 0.0) + (float) group->getDoubleAttribute ("tuning", 0.0)
                                  + (top.count ("tuning") ? (float) top["tuning"].getDoubleValue() : 0.0f);
                if (tuning != 0.0f)
                    op["tune"] = juce::String (tuning * 100.0f);
                if (chain.has ("pitchKeyTrack"))
                    op["pitch_keytrack"] = juce::String ((float) chain.get ("pitchKeyTrack").getDoubleValue() * 100.0f);
                if (chain.has ("pan"))
                    op["pan"] = chain.get ("pan");

                // The envelope (seconds; sustain 0-1), velocity tracking (0-1).
                if (chain.has ("attack")) op["ampeg_attack"] = chain.get ("attack");
                if (chain.has ("decay")) op["ampeg_decay"] = chain.get ("decay");
                if (chain.has ("sustain")) op["ampeg_sustain"] = juce::String ((float) chain.get ("sustain").getDoubleValue() * 100.0f);
                op["ampeg_release"] = chain.get ("release", "0.1");
                op["amp_veltrack"] = juce::String ((float) chain.get ("ampVelTrack", "1").getDoubleValue() * 100.0f);
                if (attackByVelocity != nullptr && ! s->hasAttribute ("attack") && ! group->hasAttribute ("attack"))
                {
                    juce::StringArray points;
                    for (const auto& [x, y] : *attackByVelocity)
                        points.add (juce::String (x, 4) + "," + juce::String (y, 4));
                    op["pr7o_velattack"] = points.joinIntoString (";");
                }

                // Loops (frames), triggers, round robins.
                if (isTrue (chain.get ("loopEnabled", "false")) && chain.has ("loopEnd"))
                {
                    op["loop_mode"] = "loop_continuous";
                    op["loop_start"] = chain.get ("loopStart", "0");
                    op["loop_end"] = chain.get ("loopEnd");
                    if (chain.has ("loopCrossfade"))
                        op["pr7o_loop_crossfade_frames"] = chain.get ("loopCrossfade");
                }
                else
                    op["loop_mode"] = "no_loop";
                const auto trigger = chain.get ("trigger", "attack").trim().toLowerCase();
                if (trigger != "attack")
                    op["trigger"] = trigger;
                const auto mode = chain.get ("seqMode", "always").trim().toLowerCase();
                const auto position = chain.get ("seqPosition", "0").getIntValue();
                if ((mode == "random" || mode == "true_random") && position > 0)
                {
                    op["lorand"] = juce::String ((float) (position - 1) / (float) randomPositions, 6);
                    op["hirand"] = juce::String ((float) position / (float) randomPositions, 6);
                }
                else if (mode == "round_robin" && position > 0)
                {
                    op["seq_length"] = juce::String (robinPositions);
                    op["seq_position"] = juce::String (position);
                }
                op["group_label"] = group->getStringAttribute ("name", "Group " + juce::String (groupIndex));
                regions.push_back (std::move (op));
            }
        }
        if (regions.empty())
            problems.add ("no samples in " + preset.getFileName());
        return regions;
    }
}
