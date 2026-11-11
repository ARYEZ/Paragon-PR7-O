#include "PartMixer.h"

namespace mixer
{
    bool StripSettings::hasEffects() const noexcept
    {
        for (const auto& slot : fx)
            if (slot.isLoaded())
                return true;
        return false;
    }

    std::unique_ptr<juce::XmlElement> StripSettings::toXml (const juce::String& tag, const juce::String& name) const
    {
        auto xml = std::make_unique<juce::XmlElement> (tag);
        if (name.isNotEmpty())
            xml->setAttribute ("name", name);
        xml->setAttribute ("level", levelDb);
        xml->setAttribute ("pan", pan);
        xml->setAttribute ("mute", mute);
        xml->setAttribute ("solo", solo);
        xml->setAttribute ("invert", invert);
        xml->setAttribute ("delay", delayDb);
        xml->setAttribute ("reverb", reverbDb);
        for (int k = 0; k < numFxSlots; ++k)
        {
            const auto& slot = fx[(size_t) k];
            if (! slot.isLoaded())
                continue;
            auto* e = xml->createNewChildElement ("Fx");
            e->setAttribute ("slot", k);
            e->setAttribute ("type", slot.type);   // mixerfx::typeNames' index (only appended to)
            e->setAttribute ("on", slot.on);
            e->setAttribute ("output", slot.outputDb);
            juce::StringArray values;
            for (const auto v : slot.values)
                values.add (juce::String (v, 6));
            e->setAttribute ("values", values.joinIntoString (" "));
        }
        return xml;
    }

    StripSettings StripSettings::fromXml (const juce::XmlElement& xml)
    {
        StripSettings s;
        s.levelDb = juce::jlimit (levelOffDb, levelTopDb, (float) xml.getDoubleAttribute ("level", 0.0));
        s.pan = juce::jlimit (-1.0f, 1.0f, (float) xml.getDoubleAttribute ("pan", 0.0));
        s.mute = xml.getBoolAttribute ("mute");
        s.solo = xml.getBoolAttribute ("solo");
        s.invert = xml.getBoolAttribute ("invert");
        s.delayDb = juce::jlimit (levelOffDb, sendTopDb, (float) xml.getDoubleAttribute ("delay", levelOffDb));
        s.reverbDb = juce::jlimit (levelOffDb, sendTopDb, (float) xml.getDoubleAttribute ("reverb", levelOffDb));
        for (const auto* e : xml.getChildWithTagNameIterator ("Fx"))
        {
            const auto k = e->getIntAttribute ("slot", -1);
            const auto type = e->getIntAttribute ("type");
            if (k < 0 || k >= numFxSlots || type <= 0 || type >= mixerfx::typeNames.size())
                continue;
            auto slot = mixerfx::defaultSlot (type);
            slot.on = e->getBoolAttribute ("on", true);
            slot.outputDb = juce::jlimit (-24.0f, 12.0f, (float) e->getDoubleAttribute ("output", 0.0));
            const auto values = juce::StringArray::fromTokens (e->getStringAttribute ("values"), " ", "");
            const auto& params = mixerfx::paramsOf (type);
            for (int i = 0; i < juce::jmin (values.size(), (int) slot.values.size(), (int) params.size()); ++i)
                slot.values[(size_t) i] = juce::jlimit (params[(size_t) i].min, params[(size_t) i].max, values[i].getFloatValue());
            s.fx[(size_t) k] = slot;
        }
        return s;
    }

    std::unique_ptr<juce::XmlElement> mixerXml (const std::map<juce::String, StripSettings>& parts, const StripSettings& section)
    {
        auto xml = std::make_unique<juce::XmlElement> ("Mixer");
        for (const auto& [name, strip] : parts)
            if (! strip.isPlain())
                xml->addChildElement (strip.toXml ("Strip", name).release());
        if (! section.isPlain())
            xml->addChildElement (section.toXml ("Section", {}).release());
        return xml;
    }

    void readMixerXml (const juce::XmlElement& xml, std::map<juce::String, StripSettings>& parts, StripSettings& section)
    {
        parts.clear();
        section = {};
        for (const auto* e : xml.getChildIterator())
        {
            if (e->hasTagName ("Strip") && e->getStringAttribute ("name").isNotEmpty())
                parts[e->getStringAttribute ("name")] = StripSettings::fromXml (*e);
            else if (e->hasTagName ("Section"))
                section = StripSettings::fromXml (*e);
        }
    }

    //==========================================================================
    PartMixer::PartMixer()
    {
        for (auto& strip : peaks)
            for (auto& p : strip)
                p = 0.0f;
    }

    void PartMixer::prepare (double sampleRate, int maxBlockSize)
    {
        rate = sampleRate;
        host.prepare (sampleRate, maxBlockSize);
        for (auto& l : live)
            for (auto* s : { &l.left, &l.right, &l.delay, &l.reverb })
                s->reset (sampleRate, 0.02);
        reset();
    }

    void PartMixer::reset()
    {
        // The gains straight to where the settings put them (no ramp from a stale value).
        currentVersion = -1;
        beginBlock();
        for (int s = 0; s < numMixerStrips; ++s)
        {
            auto& l = live[(size_t) s];
            for (auto* v : { &l.left, &l.right, &l.delay, &l.reverb })
                v->setCurrentAndTargetValue (v->getTargetValue());
        }
    }

    void PartMixer::setStrips (const std::array<StripSettings, numMixerStrips>& strips, int numParts)
    {
        // The effects first (made here, never on the audio thread), then the settings.
        FxTable wanted {};
        for (int s = 0; s < numMixerStrips; ++s)
            wanted[(size_t) s] = strips[(size_t) s].fx;
        host.sync (wanted);
        const juce::SpinLock::ScopedLockType l (lock);
        pending = strips;
        pendingParts = juce::jlimit (1, maxPartStrips, numParts);
        ++pendingVersion;
    }

    void PartMixer::beginBlock() noexcept
    {
        if (pendingVersion.load() != currentVersion)
        {
            const juce::SpinLock::ScopedTryLockType l (lock);
            if (l.isLocked())
            {
                current = pending;
                currentParts = pendingParts;
                currentVersion = pendingVersion.load();
                for (int s = 0; s < numMixerStrips; ++s)
                    table[(size_t) s] = current[(size_t) s].fx;
                host.setTable (&table);
            }
        }
        anySolo = false;
        for (int p = 0; p < currentParts; ++p)
            anySolo = anySolo || current[(size_t) p].solo;
        // Each strip's gains towards its settings: the fader x the balance x the polarity, zero when muted.
        for (int s = 0; s < numMixerStrips; ++s)
        {
            const auto& c = current[(size_t) s];
            const auto audible = ! c.mute && (s == sectionStrip || ! anySolo || c.solo);
            const auto gain = audible ? StripSettings::gainOf (c.levelDb) * (c.invert ? -1.0f : 1.0f) : 0.0f;
            auto& l = live[(size_t) s];
            l.left.setTargetValue (gain * (c.pan > 0.0f ? 1.0f - c.pan : 1.0f));
            l.right.setTargetValue (gain * (c.pan < 0.0f ? 1.0f + c.pan : 1.0f));
            l.delay.setTargetValue (StripSettings::gainOf (c.delayDb));
            l.reverb.setTargetValue (StripSettings::gainOf (c.reverbDb));
        }
    }

    void PartMixer::processPart (int part, float* left, float* right, int numSamples,
                                 float* delayLeft, float* delayRight, float* reverbLeft, float* reverbRight) noexcept
    {
        if (part < 0 || part >= maxPartStrips)
            return;
        processStrip (part, true, left, right, numSamples, delayLeft, delayRight, reverbLeft, reverbRight);
    }

    void PartMixer::processSection (float* left, float* right, int numSamples,
                                    float* delayLeft, float* delayRight, float* reverbLeft, float* reverbRight) noexcept
    {
        processStrip (sectionStrip, true, left, right, numSamples, delayLeft, delayRight, reverbLeft, reverbRight);
    }

    void PartMixer::processStrip (int s, bool, float* left, float* right, int numSamples,
                                  float* delayLeft, float* delayRight, float* reverbLeft, float* reverbRight) noexcept
    {
        auto& l = live[(size_t) s];
        // The effects (in place, in their slots' order), then the gains.
        if (host.hasEffects (s))
            host.process (s, left, right, numSamples);
        const auto settled = ! l.left.isSmoothing() && ! l.right.isSmoothing();
        const auto unity = settled && l.left.getTargetValue() == 1.0f && l.right.getTargetValue() == 1.0f;
        if (! unity)
        {
            if (settled)
            {
                juce::FloatVectorOperations::multiply (left, l.left.getTargetValue(), numSamples);
                juce::FloatVectorOperations::multiply (right, l.right.getTargetValue(), numSamples);
            }
            else
                for (int i = 0; i < numSamples; ++i)
                {
                    left[i] *= l.left.getNextValue();
                    right[i] *= l.right.getNextValue();
                }
        }
        // The sends, after the fader.
        const auto send = [numSamples, left, right] (juce::SmoothedValue<float>& g, float* toLeft, float* toRight)
        {
            if (toLeft == nullptr || toRight == nullptr || (! g.isSmoothing() && g.getTargetValue() == 0.0f))
            {
                g.skip (numSamples);
                return;
            }
            for (int i = 0; i < numSamples; ++i)
            {
                const auto v = g.getNextValue();
                toLeft[i] += left[i] * v;
                toRight[i] += right[i] * v;
            }
        };
        send (l.delay, delayLeft, delayRight);
        send (l.reverb, reverbLeft, reverbRight);
        // The meter.
        const auto peakOf = [numSamples] (const float* x)
        {
            const auto r = juce::FloatVectorOperations::findMinAndMax (x, numSamples);
            return juce::jmax (-r.getStart(), r.getEnd());
        };
        for (int side = 0; side < 2; ++side)
        {
            auto& p = peaks[(size_t) s][(size_t) side];
            const auto now = peakOf (side == 0 ? left : right);
            if (now > p.load (std::memory_order_relaxed))
                p.store (now, std::memory_order_relaxed);
        }
    }

    float PartMixer::takePeak (int strip, int side) noexcept
    {
        if (strip < 0 || strip >= numMixerStrips || side < 0 || side > 1)
            return 0.0f;
        return peaks[(size_t) strip][(size_t) side].exchange (0.0f);
    }
}
