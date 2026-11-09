#include "SendPages.h"

#include "dsp/effects/Delay.h"
#include "dsp/effects/Reverb.h"
#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    // A time axis across `area`: a line each half second, labelled.
    void timeGrid (juce::Graphics& g, juce::Rectangle<float> area, float span)
    {
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        const auto step = span > 6.0f ? 1.0f : 0.5f;
        for (auto t = 0.0f; t <= span + 1.0e-3f; t += step)
        {
            const auto x = area.getX() + area.getWidth() * t / span;
            g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
            g.setColour (ParagonColours::textDim);
            g.drawText (juce::String (t, 1) + " s", juce::Rectangle<float> (x - 30.0f, area.getBottom() + 2.0f, 60.0f, 14.0f), juce::Justification::centred, false);
        }
    }
}

//==============================================================================
EchoDisplay::EchoDisplay (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("Each echo where and how loud it comes, in dB below the note played: the first at MIX, then FEEDBACK of the one before (TONE and the "
                "TYPE's character change each one too, which isn't drawn). With PING-PONG, left above the line, right below.");
    shown = echoes();
    startTimerHz (10);
}

std::vector<EchoDisplay::Echo> EchoDisplay::echoes() const
{
    const auto settings = processor.getDelaySettings();
    const auto seconds = effects::Delay::delaySeconds (settings, juce::jmax (1.0, processor.getHostBpm()));
    std::vector<Echo> result;
    auto level = settings.mix;
    for (int k = 1; seconds > 0.0f && k * seconds <= span && level > juce::Decibels::decibelsToGain (floorDb); ++k)
    {
        result.push_back ({ (float) k * seconds, level, settings.pingPong ? (k % 2 == 1 ? -1 : 1) : 0 });
        level *= settings.feedback;
    }
    return result;
}

void EchoDisplay::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    const auto now = echoes();
    const auto same = now.size() == shown.size()
                      && std::equal (now.begin(), now.end(), shown.begin(), [] (const Echo& a, const Echo& b)
                                     { return a.seconds == b.seconds && a.level == b.level && a.side == b.side; });
    if (! same)
    {
        shown = now;
        repaint();
    }
}

void EchoDisplay::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    const auto area = getLocalBounds().toFloat().reduced (24.0f, 18.0f).withTrimmedBottom (14.0f);
    timeGrid (g, area, span);
    const auto pingPong = processor.getDelaySettings().pingPong;
    const auto middle = pingPong ? area.getCentreY() : area.getBottom();
    const auto height = pingPong ? area.getHeight() * 0.48f : area.getHeight() * 0.95f;
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (middle), area.getX(), area.getRight());
    if (pingPong)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("L", juce::Rectangle<float> (area.getX() - 20.0f, area.getY(), 16.0f, 14.0f), juce::Justification::centred, false);
        g.drawText ("R", juce::Rectangle<float> (area.getX() - 20.0f, area.getBottom() - 14.0f, 16.0f, 14.0f), juce::Justification::centred, false);
    }
    // The note played, at 0, and each echo: a bar from the line, in the menus' gradient.
    const auto barWidth = juce::jmax (4.0f, area.getWidth() * 0.008f);
    // Heights in dB, as echoes are heard: the note played at the top, -48 dB at the line.
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    for (const auto db : { -12.0f, -24.0f, -36.0f })
    {
        const auto fraction = 1.0f - db / floorDb;
        const int sides[] = { pingPong ? -1 : 0, 1 };
        for (int k = 0; k < (pingPong ? 2 : 1); ++k)
        {
            const auto side = sides[k];
            const auto y = side > 0 ? middle + height * fraction : middle - height * fraction;
            g.setColour (ParagonColours::panelEdge.withAlpha (0.35f));
            g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
            g.setColour (ParagonColours::textDim.withAlpha (0.7f));
            g.drawText (juce::String ((int) db) + " dB", juce::Rectangle<float> (area.getRight() - 46.0f, y - 12.0f, 44.0f, 11.0f),
                        juce::Justification::centredRight, false);
        }
    }
    const auto stem = [&] (float seconds, float level, int side, bool played)
    {
        const auto x = area.getX() + area.getWidth() * seconds / span;
        const auto h = height * juce::jlimit (0.0f, 1.0f, 1.0f - juce::Decibels::gainToDecibels (level, floorDb) / floorDb);
        const auto bar = side > 0 ? juce::Rectangle<float> (x - barWidth * 0.5f, middle, barWidth, h)
                                  : juce::Rectangle<float> (x - barWidth * 0.5f, middle - h, barWidth, h);
        if (played)
            g.setColour (ParagonColours::textDim.withAlpha (0.6f));
        else
            g.setGradientFill (ParagonLookAndFeel::menuHighlight (bar.withHeight (juce::jmax (1.0f, bar.getHeight()))));
        g.fillRoundedRectangle (bar, 1.5f);
    };
    stem (0.0f, 1.0f, pingPong ? -1 : 0, true);
    if (pingPong)
        stem (0.0f, 1.0f, 1, true);
    for (const auto& e : shown)
        stem (e.seconds, e.level, e.side, false);
}

//==============================================================================
DecayDisplay::DecayDisplay (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("The reverb's tail: silence for PRE-DELAY, then fading to -60 dB over the time SIZE and the TYPE give.");
    timerCallback();
    startTimerHz (10);
}

void DecayDisplay::timerCallback()
{
    if (! isShowing())   // 0.30.0: a hidden page's timer does nothing
        return;
    const auto settings = processor.getReverbSettings();
    const auto newDecay = effects::Reverb::decaySeconds (settings.type, settings.size);
    const auto newPre = settings.preDelayMs * 0.001f;
    if (newDecay != decay || newPre != preDelay)
    {
        decay = newDecay;
        preDelay = newPre;
        span = juce::jmax (2.0f, std::ceil ((preDelay + decay) * 1.15f));
        repaint();
    }
}

void DecayDisplay::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    const auto area = getLocalBounds().toFloat().reduced (24.0f, 18.0f).withTrimmedBottom (14.0f).withTrimmedLeft (18.0f);
    timeGrid (g, area, span);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (const auto db : { 0.0f, -20.0f, -40.0f, -60.0f })
    {
        const auto y = area.getY() + area.getHeight() * -db / 60.0f;
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (area.getX() - 38.0f, y - 7.0f, 34.0f, 14.0f), juce::Justification::centredRight, false);
    }
    // Silence, then from 0 dB a straight fall (in dB) to -60 at the decay time.
    const auto xAt = [&] (float seconds) { return area.getX() + area.getWidth() * seconds / span; };
    juce::Path tail;
    tail.startNewSubPath (xAt (preDelay), area.getBottom());
    tail.lineTo (xAt (preDelay), area.getY());
    tail.lineTo (xAt (preDelay + decay), area.getBottom());
    tail.closeSubPath();
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
    g.setOpacity (0.55f);
    g.fillPath (tail);
    g.setOpacity (1.0f);
    g.setColour (ParagonColours::text);
    g.drawLine (xAt (preDelay), area.getY(), xAt (preDelay + decay), area.getBottom(), 2.0f);
    ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    ParagonColours::setTextFill (g, ParagonColours::accent, ParagonColours::textSpan (g.getCurrentFont(), "TAIL " + juce::String (decay, decay < 10.0f ? 1 : 0) + " s  (PRE-DELAY " + juce::String (juce::roundToInt (preDelay * 1000.0f)) + " ms)", (area.reduced (10.0f, 6.0f)).toFloat(), juce::Justification::topRight));
    g.drawText ("TAIL " + juce::String (decay, decay < 10.0f ? 1 : 0) + " s  (PRE-DELAY " + juce::String (juce::roundToInt (preDelay * 1000.0f)) + " ms)",
                area.reduced (10.0f, 6.0f), juce::Justification::topRight, false);
    ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());
}

//==============================================================================
SendPage::SendPage (ParagonX1AudioProcessor& p, Kind k) : kind (k)
{
    auto& state = p.getParameters();
    using namespace ParamIDs;
    const auto make = [&state] (auto& list, const juce::String& id, const juce::String& label, const juce::String& tip)
    {
        list.push_back (std::make_unique<ParamControl> (state, id, label, tip));
        return list.back().get();
    };
    if (kind == Kind::delay)
    {
        display = std::make_unique<EchoDisplay> (p);
        make (top, delayOn, "ON", "The DELAY on or off. Switched off, its echoes ring out rather than stopping dead.");
        make (top, delayType, "TYPE", "DIGITAL clean repeats; ANALOG warm and dark, a bucket-brigade pedal's; TAPE with wow, flutter and hiss; "
                                      "MOD chorused; REVERSE backwards; MULTI-TAP rhythmic taps in each beat; SHIMMER each repeat an octave up; "
                                      "DUCKING tucked under the playing.");
        make (controls, delayTime, "TIME", "The echo time in milliseconds, when SYNC is off.");
        make (controls, delaySync, "SYNC", "The echo time in notes at the song's tempo (OFF: TIME in milliseconds).");
        make (controls, delayFeedback, "FEEDBACK", "How much of each echo comes back again: more repeats, longer.");
        make (controls, delayTone, "TONE", "How bright the echoes are; each repeat a little darker than the one before.");
        make (controls, delayMix, "MIX", "How loud the echoes come back into the mix (the return's level).");
        make (controls, delayPingPong, "PING-PONG", "The echoes bounce left, right, left...: what's sent goes in as one, so even a centred sound bounces.");
        make (controls, delayDuck, "DUCK", "Tucks the echoes under what you play; they swell back in the gaps.");
        make (controls, delayToReverb, "> REVERB", "How much of the echoes go on into the REVERB (when it's on): echoes with their own space.");
    }
    else
    {
        display = std::make_unique<DecayDisplay> (p);
        make (top, reverbOn, "ON", "The REVERB on or off. Switched off, its tail rings out.");
        make (top, reverbType, "TYPE", "CLASSIC the original; ROOM small, close; HALL long and smooth; CATHEDRAL huge and dark; PLATE dense "
                                       "and bright; SPRING an amp's spring tank; DIGITAL an 80s rack's lush tail; REVERSE swelling into each note.");
        make (controls, reverbSize, "SIZE", "How long the tail lasts (REVERSE: the length of each swell).");
        make (controls, reverbPreDelay, "PRE-DELAY", "The gap before the reverb starts: keeps the attack clear.");
        make (controls, reverbTone, "TONE", "How bright the reverb is.");
        make (controls, reverbMix, "MIX", "How loud the reverb comes back into the mix (the return's level).");
        make (controls, reverbDuck, "DUCK", "Tucks the reverb under what you play; it swells back in the gaps.");
    }
    make (sends, kind == Kind::delay ? instSendDelay : instSendReverb, "INSTRUMENT",
          juce::String ("How much of the instrument goes to the ") + (kind == Kind::delay ? "DELAY" : "REVERB")
              + " (also on the INSTRUMENT page): 0 keeps it dry. On an insert bus it sends through the bus instead.");
    for (auto* list : { &top, &controls, &sends })
        for (auto& c : *list)
            addAndMakeVisible (*c);
    addAndMakeVisible (*display);
    back.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (back);
    sendsLabel.setText ("SENDS", juce::dontSendNotification);
    sendsLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    sendsLabel.setColour (juce::Label::textColourId, ParagonColours::textDim);
    addAndMakeVisible (sendsLabel);
}

std::vector<ParamControl*> SendPage::getControls()
{
    std::vector<ParamControl*> result;
    for (auto* list : { &top, &controls })
        for (auto& c : *list)
            result.push_back (c.get());
    return result;
}

std::vector<ParamControl*> SendPage::getSends()
{
    std::vector<ParamControl*> result;
    for (auto& c : sends)
        result.push_back (c.get());
    return result;
}

void SendPage::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (juce::Rectangle<float> (24.0f, 18.0f, 120.0f, 24.0f)));
    g.drawText (kind == Kind::delay ? "DELAY" : "REVERB", juce::Rectangle<int> (24, 14, 160, 26), juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (kind == Kind::delay ? "Send 1: echoes" : "Send 2: space", juce::Rectangle<int> (24, 38, 200, 16), juce::Justification::centredLeft, false);
}

void SendPage::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    auto bar = area.removeFromTop (54);
    bar.removeFromLeft (190);
    back.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, 30));
    bar.removeFromRight (20);
    top[0]->setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (10);
    top[1]->setBounds (bar.removeFromLeft (180));
    area.removeFromTop (8);

    const auto h = ParamControl::knobHeight;
    auto sendRow = area.removeFromBottom (h);
    area.removeFromBottom (4);
    sendsLabel.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (8);
    auto controlRow = area.removeFromBottom (h);
    area.removeFromBottom (10);
    display->setBounds (area);
    std::vector<juce::Component*> row, sendList;
    for (auto& c : controls)
        row.push_back (c.get());
    for (auto& c : sends)
        sendList.push_back (c.get());
    SynthPanel::layoutRow (controlRow, row, 110);
    SynthPanel::layoutRow (sendRow, sendList, 110);
}

//==============================================================================
SendMatrix::SendMatrix (ParagonX1AudioProcessor& p)
{
    auto& state = p.getParameters();
    for (int which = 0; which < fx::numSends; ++which)
    {
        knobs.push_back (std::make_unique<ParamControl> (state, which == 0 ? ParamIDs::instSendDelay : ParamIDs::instSendReverb,
                                                         which == 0 ? "TO DELAY" : "TO REVERB",
                                                         juce::String ("How much of the instrument goes to the ") + (which == 0 ? "DELAY" : "REVERB")
                                                             + " (0: it stays dry). On an insert bus, the bus's own sends are used instead."));
        addAndMakeVisible (*knobs.back());
    }
    delayToReverb = std::make_unique<ParamControl> (state, ParamIDs::delayToReverb, "DELAY > REVERB",
                                                    "How much of the DELAY's echoes go on into the REVERB");
    addAndMakeVisible (*delayToReverb);
}

void SendMatrix::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("THE INSTRUMENT'S SENDS", juce::Rectangle<int> (14, 6, 300, 20), juce::Justification::centredLeft, false);
}

void SendMatrix::resized()
{
    auto area = getLocalBounds().reduced (14, 6);
    area.removeFromTop (20);
    SynthPanel::layoutRow (area.withSizeKeepingCentre (area.getWidth(), ParamControl::knobHeight),
                           { knobs[0].get(), knobs[1].get(), delayToReverb.get() }, 130, 40);
}
