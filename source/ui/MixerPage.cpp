#include "MixerPage.h"

#include "NoteNames.h"
#include "dsp/mixer/MixerEffects.h"
#include "library/Library.h"
#include "ui/DockPanels.h"
#include "ui/Finish.h"
#include "ui/LedDisplay.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/PedalBody.h"

namespace
{
    // As the INSTRUMENT page's rack unit (InstrumentRack.cpp): the chassis, its light, the screens.
    const PedalBody::Shape unitShape { 16.0f, 10.0f, 5.0f, 0.3f };
    constexpr float screenSize = 0.45f;
    const auto orange = LedDisplay::ledOrange;
    constexpr int titleHeight = 34;

    juce::FontOptions font (float h, int style = juce::Font::bold) { return juce::FontOptions (h, style); }

    void stamped (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                  juce::Justification j = juce::Justification::centred)
    {
        g.setFont (font (height));
        ParagonLookAndFeel::drawStampedText (g, text, area, j, Finish::ink());
    }

    // A red printed rule with a word in its middle (as the rack's brackets).
    void printed (juce::Graphics& g, const juce::String& word, juce::Rectangle<float> area)
    {
        g.setColour (Finish::printedRed());
        g.setFont (font (8.0f));
        g.drawText (word, area, juce::Justification::centred, false);
        const auto gap = juce::GlyphArrangement::getStringWidth (font (8.0f), word) * 0.5f + 6.0f;
        const auto y = area.getCentreY();
        g.fillRect (juce::Rectangle<float> (area.getX(), y, juce::jmax (0.0f, area.getWidth() * 0.5f - gap), 1.2f));
        g.fillRect (juce::Rectangle<float> (area.getCentreX() + gap, y, juce::jmax (0.0f, area.getWidth() * 0.5f - gap), 1.2f));
    }

    // A value on a small screen.
    void valueScreen (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, float textHeight = 10.5f)
    {
        const auto glass = LedDisplay::paintScreen (g, area, screenSize);
        g.setColour (orange);
        g.setFont (font (textHeight));
        g.drawFittedText (text, glass.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    juce::String dbText (float db, float offDb = mixer::levelOffDb)
    {
        if (db <= offDb + 0.05f)
            return "OFF";
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    juce::String panText (float pan)
    {
        if (std::abs (pan) < 0.005f)
            return "C";
        return juce::String (juce::roundToInt (std::abs (pan) * 100.0f)) + (pan < 0.0f ? " L" : " R");
    }

    void key (juce::TextButton& b, juce::Component& parent)
    {
        b.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
        b.setClickingTogglesState (false);
        parent.addAndMakeVisible (b);
    }

    void rackKnob (juce::Slider& s, juce::Component& parent)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.getProperties().set (ParagonLookAndFeel::pedalKnobProperty, true);
        parent.addAndMakeVisible (s);
    }
}

//==============================================================================
// THE FADER: a slot, its scale (+12 at the top, 0 a quarter down, -60 at the foot), a key for its cap. Drag it (with
// Shift: finely); double-click: 0 dB.
class Fader final : public juce::Component
{
public:
    Fader()
    {
        cap.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
        cap.setToggleState (true, juce::dontSendNotification);
        cap.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (cap);
        setRepaintsOnMouseActivity (false);
    }

    std::function<void (float db, bool final)> onChange;
    float getDb() const noexcept { return db; }
    void setDb (float newDb)
    {
        db = juce::jlimit (mixer::levelOffDb, mixer::levelTopDb, newDb);
        resized();
    }

    static float yOf (float value) noexcept
    {
        if (value >= 0.0f)
            return 0.25f * (mixer::levelTopDb - value) / mixer::levelTopDb;
        return 0.25f + 0.75f * juce::jmin (1.0f, -value / -mixer::levelOffDb);
    }
    static float dbOf (float y) noexcept
    {
        y = juce::jlimit (0.0f, 1.0f, y);
        if (y <= 0.25f)
            return mixer::levelTopDb * (1.0f - y / 0.25f);
        return mixer::levelOffDb * (y - 0.25f) / 0.75f;
    }

    juce::Rectangle<float> slot() const { return getLocalBounds().toFloat().withSizeKeepingCentre (8.0f, (float) getHeight() - 20.0f); }

    void resized() override
    {
        const auto s = slot();
        cap.setBounds (juce::Rectangle<float> (s.getCentreX() - 21.0f, s.getY() + s.getHeight() * yOf (db) - 10.0f, 42.0f, 20.0f).toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        const auto s = slot();
        g.setColour (juce::Colour (0xff050405));
        g.fillRoundedRectangle (s, 3.0f);
        g.setColour (juce::Colour (0xff2a2128));
        g.drawRoundedRectangle (s, 3.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        startDb = db;
        startY = e.position.y;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto s = slot();
        const auto moved = (e.position.y - startY) / s.getHeight() * (e.mods.isShiftDown() ? 0.2f : 1.0f);
        setDb (dbOf (yOf (startDb) + moved));
        if (onChange != nullptr)
            onChange (db, false);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (onChange != nullptr)
            onChange (db, true);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        setDb (0.0f);
        if (onChange != nullptr)
            onChange (db, true);
    }

private:
    juce::TextButton cap;
    float db = 0.0f, startDb = 0.0f, startY = 0.0f;
};

// An effect slot's screen: its effect's name (or + ADD EFFECT, dim); clicked, it opens the list or the editor.
class SlotScreen final : public juce::Component
{
public:
    std::function<void()> onClick;
    juce::String text;
    bool loaded = false;
    void paint (juce::Graphics& g) override
    {
        const auto glass = LedDisplay::paintScreen (g, getLocalBounds().toFloat(), screenSize);
        g.setColour (loaded ? orange : orange.withAlpha (isMouseOver() ? 0.6f : 0.3f));
        g.setFont (font (11.0f));
        g.drawFittedText (loaded ? text : juce::String ("+ ADD EFFECT"), glass.reduced (8.0f, 0.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.7f);
        LedDisplay::paintGlassOverlay (g, glass, screenSize);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick != nullptr)
            onClick();
    }
};

//==============================================================================
class MixerPage::Strip final : public juce::Component
{
public:
    Strip (MixerPage& p, int i, bool isSection) : page (p), index (i), section (isSection)
    {
        setOpaque (false);
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            key (fxKeys[k], *this);
            fxKeys[k].setTooltip ("Switches the slot's effect on or off (its settings kept)");
            fxKeys[k].onClick = [this, k] { change ([k] (auto& s) { s.fx[(size_t) k].on = ! s.fx[(size_t) k].on; }); };
            addAndMakeVisible (slots[k]);
            slots[k].onClick = [this, k]
            {
                if (page.settingsOf (index).fx[(size_t) k].isLoaded())
                    page.openEditor (index, k);
                else
                    page.showSlotMenu (index, k);
            };
        }
        for (auto* s : { &delay, &reverb })
        {
            rackKnob (*s, *this);
            s->setNormalisableRange ({ mixer::levelOffDb, mixer::sendTopDb, 0.0, 2.2 });
            s->setDoubleClickReturnValue (true, mixer::levelOffDb);
        }
        delay.setTooltip ("SEND DELAY: a copy into the FX page's DELAY, after the fader (while DELAY is on)");
        reverb.setTooltip ("SEND REVERB: a copy into the FX page's REVERB, after the fader (while REVERB is on)");
        rackKnob (pan, *this);
        pan.setRange (-1.0, 1.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTooltip ("PAN: a balance, the far side turned down");
        delay.onValueChange = [this] { change ([this] (auto& s) { s.delayDb = (float) delay.getValue(); }, false); repaint(); };
        reverb.onValueChange = [this] { change ([this] (auto& s) { s.reverbDb = (float) reverb.getValue(); }, false); repaint(); };
        pan.onValueChange = [this] { change ([this] (auto& s) { s.pan = (float) pan.getValue(); }, false); repaint(); };
        for (auto* s : { &delay, &reverb, &pan })
            s->onDragEnd = [this] { change ([] (auto&) {}, true); };
        addAndMakeVisible (fader);
        fader.onChange = [this] (float db, bool final) { change ([db] (auto& s) { s.levelDb = db; }, final); repaint(); };
        key (mute, *this);
        key (solo, *this);
        key (invert, *this);
        mute.setTooltip ("MUTE: silent (its sends too)");
        solo.setTooltip ("SOLO: only the soloed parts heard");
        invert.setTooltip ("PHASE: the polarity flipped");
        mute.onClick = [this] { change ([] (auto& s) { s.mute = ! s.mute; }); };
        solo.onClick = [this] { change ([] (auto& s) { s.solo = ! s.solo; }); };
        invert.onClick = [this] { change ([] (auto& s) { s.invert = ! s.invert; }); };
        solo.setVisible (! section);   // (the section has no other strip beside it to solo against)
        satin = Finish::makeSatinGrain (400, 1400, 77 + i);
    }

    void change (const std::function<void (mixer::StripSettings&)>& edit, bool final = true)
    {
        if (updating)
            return;
        auto s = page.settingsOf (index);
        edit (s);
        page.apply (index, s, final);
        show (s);
    }

    void show (const mixer::StripSettings& s)
    {
        const juce::ScopedValueSetter<bool> busy (updating, true);
        shown = s;
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            const auto& slot = s.fx[(size_t) k];
            fxKeys[k].setToggleState (slot.isLoaded() && slot.on, juce::dontSendNotification);
            fxKeys[k].setEnabled (slot.isLoaded());
            slots[k].loaded = slot.isLoaded();
            slots[k].text = slot.isLoaded() ? mixerfx::typeNames[slot.type].toUpperCase() : juce::String();
            slots[k].repaint();
        }
        if (! delay.isMouseButtonDown()) delay.setValue (s.delayDb, juce::dontSendNotification);
        if (! reverb.isMouseButtonDown()) reverb.setValue (s.reverbDb, juce::dontSendNotification);
        if (! pan.isMouseButtonDown()) pan.setValue (s.pan, juce::dontSendNotification);
        if (! fader.isMouseButtonDown()) fader.setDb (s.levelDb);
        mute.setToggleState (s.mute, juce::dontSendNotification);
        solo.setToggleState (s.solo, juce::dontSendNotification);
        invert.setToggleState (s.invert, juce::dontSendNotification);
        repaint();
    }

    void setName (const juce::String& title, const juce::String& line, const juce::String& caption)
    {
        name = title;
        keysText = line;
        captionText = caption;
        repaint();
    }
    void setOutput (const juce::String& o) { if (o != output) { output = o; repaint(); } }

    void meter (float left, float right)
    {
        // Peaks to dB, falling 26 dB a second at the 30 Hz timer.
        const auto toDb = [] (float x) { return juce::Decibels::gainToDecibels (x, -90.0f); };
        meterDb[0] = juce::jmax (toDb (left), meterDb[0] - 0.9f);
        meterDb[1] = juce::jmax (toDb (right), meterDb[1] - 0.9f);
        repaint (meterArea().expanded (6.0f).toNearestInt());
    }

    // The layout (the fader takes what's left of the height).
    juce::Rectangle<float> face() const { return getLocalBounds().toFloat().reduced (7.0f).withTrimmedBottom (unitShape.frontEdge).reduced (7.0f); }
    juce::Rectangle<float> content() const { return face().reduced (10.0f, 10.0f); }
    juce::Rectangle<float> nameScreen() const { return content().withHeight (60.0f); }
    juce::Rectangle<float> fxRow (int k) const { return content().withTrimmedTop (86.0f + 29.0f * (float) k).withHeight (25.0f); }
    juce::Rectangle<float> knobsArea() const { return content().withTrimmedTop (206.0f).withHeight (96.0f); }
    juce::Rectangle<float> levelArea() const { return content().withTrimmedTop (306.0f).withTrimmedBottom (102.0f); }
    juce::Rectangle<float> keysRow() const { return content().withY (content().getBottom() - 94.0f).withHeight (40.0f); }
    juce::Rectangle<float> outputScreen() const { return content().withY (content().getBottom() - 40.0f).withHeight (40.0f); }
    juce::Rectangle<float> faderBody() const { return levelArea().withTrimmedTop (14.0f).withTrimmedBottom (26.0f); }
    juce::Rectangle<float> meterArea() const
    {
        const auto body = faderBody();
        return body.withX (body.getX() + body.getWidth() * 0.62f).withWidth (24.0f).reduced (0.0f, 10.0f);
    }

    void resized() override
    {
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            const auto row = fxRow (k);
            fxKeys[k].setBounds (row.withWidth (32.0f).withSizeKeepingCentre (30.0f, 22.0f).toNearestInt());
            slots[k].setBounds (row.withTrimmedLeft (38.0f).toNearestInt());
        }
        const auto knobs = knobsArea().withTrimmedTop (16.0f);
        const auto third = knobs.getWidth() / 3.0f;
        const auto size = juce::jlimit (36.0f, 52.0f, third - 8.0f);
        juce::Slider* row[] = { &delay, &reverb, &pan };
        for (int i = 0; i < 3; ++i)
            row[i]->setBounds (knobs.withX (knobs.getX() + third * (float) i).withWidth (third).withHeight (size)
                                    .withSizeKeepingCentre (size, size).toNearestInt());
        const auto body = faderBody();
        fader.setBounds (body.withX (body.getX() + body.getWidth() * 0.32f - 24.0f).withWidth (48.0f).toNearestInt());
        const auto keys = keysRow();
        const auto w = keys.getWidth() / 3.0f;
        juce::TextButton* row2[] = { &mute, &solo, &invert };
        for (int i = 0; i < 3; ++i)
            row2[i]->setBounds (keys.withX (keys.getX() + w * (float) i).withWidth (w).withHeight (26.0f).withSizeKeepingCentre (40.0f, 26.0f).toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        Finish::paintAs (Finish::Type::pearlBlack);
        const auto chassis = getLocalBounds().toFloat().reduced (7.0f);
        PedalBody::paintUnit (g, chassis, face(), [this] (juce::Graphics& gg) { gg.drawImage (satin, getLocalBounds().toFloat()); }, unitShape);

        // The name.
        {
            const auto glass = LedDisplay::paintScreen (g, nameScreen(), screenSize);
            auto t = glass.reduced (9.0f, 5.0f);
            g.setColour (orange.withAlpha (0.6f));
            g.setFont (font (8.5f));
            g.drawText (captionText, t.removeFromTop (11.0f), juce::Justification::centredLeft, false);
            g.setColour (orange);
            g.setFont (font (18.0f));
            g.drawFittedText (name, t.removeFromTop (22.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
            g.setColour (orange.withAlpha (0.65f));
            g.setFont (font (9.5f));
            g.drawFittedText (keysText, t.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
        printed (g, "EFFECTS", content().withTrimmedTop (68.0f).withHeight (12.0f));
        printed (g, "SENDS AND PAN", knobsArea().withHeight (12.0f));
        const auto named = [&] (const juce::Slider& s, const juce::String& n, const juce::String& v)
        {
            const auto k = s.getBounds().toFloat();
            stamped (g, n, k.translated (0.0f, k.getHeight() + 1.0f).withHeight (11.0f).expanded (20.0f, 0.0f), 8.0f);
            valueScreen (g, k.withY (k.getBottom() + 12.0f).withHeight (17.0f).withSizeKeepingCentre (juce::jmin (64.0f, getWidth() / 3.0f - 8.0f), 17.0f), v);
        };
        named (delay, "DELAY", dbText (shown.delayDb));
        named (reverb, "REVERB", dbText (shown.reverbDb));
        named (pan, "PAN", panText (shown.pan));

        // LEVEL: the scale beside the fader, the meter, the level.
        printed (g, "LEVEL", levelArea().withHeight (12.0f));
        const auto slot = fader.getBounds().toFloat().withSizeKeepingCentre (8.0f, (float) fader.getHeight() - 20.0f);
        for (const auto mark : { 12.0f, 0.0f, -12.0f, -24.0f, -60.0f })
        {
            const auto y = slot.getY() + slot.getHeight() * Fader::yOf (mark);
            g.setColour (Finish::ink().withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (slot.getRight() + 16.0f, y - 0.5f, 7.0f, 1.0f));
            stamped (g, mark > 0 ? "+" + juce::String ((int) mark) : juce::String ((int) mark),
                     juce::Rectangle<float> (slot.getX() - 46.0f, y - 6.0f, 28.0f, 12.0f), 7.5f, juce::Justification::centredRight);
        }
        const auto glass = LedDisplay::paintScreen (g, meterArea().expanded (4.0f, 4.0f), 0.3f);
        for (int side = 0; side < 2; ++side)
        {
            const auto barWidth = (glass.getWidth() - 9.0f) * 0.5f;
            const auto bar = glass.reduced (3.0f, 2.0f).withWidth (barWidth).translated ((float) side * (barWidth + 3.0f), 0.0f);
            const auto top = bar.getY() + bar.getHeight() * Fader::yOf (juce::jlimit (mixer::levelOffDb, mixer::levelTopDb, meterDb[side]));
            const juce::ColourGradient grad (juce::Colour (0xffff2d6e), 0.0f, bar.getBottom(), juce::Colour (0xffff6a24), 0.0f, bar.getY(), false);
            for (auto y = bar.getBottom() - 3.0f; y > bar.getY(); y -= 4.0f)
            {
                g.setColour (y > top ? grad.getColourAtPosition ((bar.getBottom() - y) / bar.getHeight()) : juce::Colour (0xff2a0f16));
                g.fillRect (juce::Rectangle<float> (bar.getX(), y, bar.getWidth(), 2.5f));
            }
        }
        LedDisplay::paintGlassOverlay (g, glass, 0.3f);
        valueScreen (g, levelArea().withY (faderBody().getBottom() + 4.0f).withHeight (20.0f).withSizeKeepingCentre (84.0f, 20.0f),
                     dbText (shown.levelDb), 11.5f);

        const char* keyNames[] = { "MUTE", "SOLO", "PHASE" };
        const juce::TextButton* keyButtons[] = { &mute, &solo, &invert };
        for (int i = 0; i < 3; ++i)
            if (keyButtons[i]->isVisible())
            {
                const auto k = keyButtons[i]->getBounds().toFloat();
                stamped (g, keyNames[i], k.translated (0.0f, k.getHeight() + 2.0f).withHeight (11.0f).expanded (16.0f, 0.0f), 7.5f);
            }
        {
            const auto v = LedDisplay::paintScreen (g, outputScreen(), screenSize);
            auto t = v.reduced (9.0f, 3.0f);
            g.setColour (orange.withAlpha (0.6f));
            g.setFont (font (8.0f));
            g.drawText ("OUTPUT", t.removeFromTop (11.0f), juce::Justification::centredLeft, false);
            g.setColour (orange);
            g.setFont (font (12.0f));
            g.drawFittedText (output, t.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        for (const auto& area : { nameScreen(), outputScreen() })
            LedDisplay::paintGlassOverlay (g, area.reduced (3.0f), screenSize);
        Finish::paintAs (std::nullopt);
    }

    MixerPage& page;
    const int index;
    const bool section;
    juce::TextButton fxKeys[mixer::numFxSlots], mute, solo, invert;
    SlotScreen slots[mixer::numFxSlots];
    juce::Slider delay, reverb, pan;
    Fader fader;
    mixer::StripSettings shown;
    juce::String name, keysText, captionText, output;
    float meterDb[2] { -90.0f, -90.0f };
    bool updating = false;
    juce::Image satin;
};

//==============================================================================
// THE EFFECT's editor: a rack unit over the strips: the part and slot on a screen, ON, CHANGE, REMOVE, CLOSE keys;
// a knob for each of the effect's settings (and OUTPUT), its name stamped under it, its value on a screen.
class MixerPage::EffectEditor final : public juce::Component
{
public:
    explicit EffectEditor (MixerPage& p) : page (p)
    {
        for (auto* k : { &onKey, &changeKey, &removeKey, &closeKey })
            key (*k, *this);
        onKey.onClick = [this] { edit ([] (mixer::FxSlot& s) { s.on = ! s.on; }); };
        changeKey.onClick = [this] { page.showSlotMenu (strip, slot); };
        removeKey.onClick = [this] { page.chooseEffect (strip, slot, 0); page.closeEditor(); };
        closeKey.onClick = [this] { page.closeEditor(); };
        onKey.setTooltip ("ON: the effect on or off (its settings kept)");
        changeKey.setTooltip ("Another effect in this slot");
        removeKey.setTooltip ("Empties the slot");
        satin = Finish::makeSatinGrain (1600, 800, 99);
    }

    void open (int stripIndex, int slotIndex)
    {
        strip = stripIndex;
        slot = slotIndex;
        rebuild();
        setVisible (true);
        toFront (false);
    }
    int getStripIndex() const noexcept { return strip; }
    int getSlotIndex() const noexcept { return slot; }
    int getNumKnobs() const noexcept { return (int) knobs.size(); }
    juce::Slider& getKnob (int i) noexcept { return *knobs[(size_t) i]; }   // the type's settings, then OUTPUT

    void rebuild()
    {
        knobs.clear();
        const auto s = current();
        type = s.type;
        const auto& params = mixerfx::paramsOf (type);
        for (int i = 0; i <= (int) params.size(); ++i)
        {
            auto k = std::make_unique<juce::Slider>();
            rackKnob (*k, *this);
            if (i < (int) params.size())
            {
                const auto& p = params[(size_t) i];
                if (! p.choices.isEmpty())
                    k->setRange (0.0, (double) p.choices.size() - 1.0, 1.0);
                else
                {
                    juce::NormalisableRange<double> range (p.min, p.max);
                    if (p.log && p.min > 0.0f)
                        range.setSkewForCentre (std::sqrt ((double) p.min * (double) p.max));
                    k->setNormalisableRange (range);
                }
                k->setDoubleClickReturnValue (true, p.def);
                k->setValue (s.values[(size_t) i], juce::dontSendNotification);
                k->onValueChange = [this, i, raw = k.get()] { edit ([i, raw] (mixer::FxSlot& f) { f.values[(size_t) i] = (float) raw->getValue(); }, false); };
            }
            else
            {
                k->setRange (-24.0, 12.0);
                k->setDoubleClickReturnValue (true, 0.0);
                k->setValue (s.outputDb, juce::dontSendNotification);
                k->onValueChange = [this, raw = k.get()] { edit ([raw] (mixer::FxSlot& f) { f.outputDb = (float) raw->getValue(); }, false); };
            }
            k->onDragEnd = [this] { edit ([] (mixer::FxSlot&) {}, true); };
            knobs.push_back (std::move (k));
        }
        onKey.setToggleState (s.on, juce::dontSendNotification);
        resized();
        repaint();
    }

    void show()
    {
        // From the processor (an undo, a preset): the knobs where the settings are.
        const auto s = current();
        if (s.type != type)
        {
            if (! s.isLoaded())
            {
                page.closeEditor();
                return;
            }
            rebuild();
            return;
        }
        for (int i = 0; i < (int) knobs.size(); ++i)
            if (! knobs[(size_t) i]->isMouseButtonDown())
                knobs[(size_t) i]->setValue (i < (int) mixerfx::paramsOf (type).size() ? s.values[(size_t) i] : s.outputDb,
                                             juce::dontSendNotification);
        onKey.setToggleState (s.on, juce::dontSendNotification);
        repaint();
    }

    // The unit sized to its knobs (rows of up to 10), in the middle of the dimmed strips.
    int rows() const noexcept { return juce::jmax (1, (numSettings() + perRow() - 1) / perRow()); }
    juce::Rectangle<float> unit() const
    {
        const auto height = juce::jmin ((float) getHeight(), 170.0f + 124.0f * (float) rows() + 40.0f);
        return getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), height);
    }
    juce::Rectangle<float> face() const { return unit().reduced (7.0f).withTrimmedBottom (unitShape.frontEdge).reduced (9.0f); }
    juce::Rectangle<float> content() const { return face().reduced (16.0f, 12.0f); }
    juce::Rectangle<float> screen() const { return content().withWidth (300.0f).withHeight (78.0f); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! unit().contains (e.position))   // the dimmed strips round it: closes it
            page.closeEditor();
    }
    juce::Rectangle<float> knobsArea() const { return content().withTrimmedTop (110.0f); }
    // The type's settings in rows of up to 10 (OUTPUT, the last knob, sits up beside the keys).
    int numSettings() const noexcept { return juce::jmax (0, (int) knobs.size() - 1); }
    int perRow() const noexcept { return juce::jmax (1, juce::jmin (numSettings(), 10)); }

    void resized() override
    {
        auto keys = content().withTrimmedLeft (330.0f).withHeight (40.0f);
        for (auto* k : { &onKey, &changeKey, &removeKey })
        {
            k->setBounds (keys.removeFromLeft (90.0f).withHeight (26.0f).withSizeKeepingCentre (54.0f, 26.0f).toNearestInt());
            keys.removeFromLeft (10.0f);
        }
        closeKey.setBounds (content().withTrimmedLeft (content().getWidth() - 90.0f).withHeight (26.0f).withSizeKeepingCentre (54.0f, 26.0f).toNearestInt());
        const auto area = knobsArea();
        const auto n = perRow();
        const auto w = juce::jmin (140.0f, area.getWidth() / (float) n);
        const auto left = area.getX() + (area.getWidth() - w * (float) n) * 0.5f;   // centred
        for (int i = 0; i < numSettings(); ++i)
        {
            const auto cell = juce::Rectangle<float> (left + w * (float) (i % n), area.getY() + 124.0f * (float) (i / n), w, 64.0f);
            knobs[(size_t) i]->setBounds (cell.withSizeKeepingCentre (62.0f, 62.0f).toNearestInt());
        }
        if (! knobs.empty())   // OUTPUT, beside the keys (left of CLOSE)
            knobs.back()->setBounds (juce::Rectangle<float> (closeKey.getX() - 150.0f, content().getY(), 62.0f, 50.0f)
                                         .withSizeKeepingCentre (50.0f, 50.0f).toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        Finish::paintAs (Finish::Type::pearlBlack);
        g.fillAll (juce::Colour (0xc0060406));   // the strips dimmed behind it
        const auto chassis = unit().reduced (7.0f);
        PedalBody::paintUnit (g, chassis, face(), [this] (juce::Graphics& gg) { gg.drawImage (satin, getLocalBounds().toFloat()); }, unitShape);
        const auto s = current();
        {
            const auto glass = LedDisplay::paintScreen (g, screen(), screenSize);
            auto t = glass.reduced (12.0f, 7.0f);
            g.setColour (orange.withAlpha (0.6f));
            g.setFont (font (8.5f));
            g.drawText (page.getStripName (strip) + "  /  SLOT " + juce::String (slot + 1), t.removeFromTop (11.0f), juce::Justification::centredLeft, false);
            g.setColour (orange);
            g.setFont (font (22.0f));
            g.drawFittedText (mixerfx::typeNames[juce::jlimit (0, mixerfx::typeNames.size() - 1, s.type)].toUpperCase(),
                              t.removeFromTop (30.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (orange.withAlpha (0.65f));
            g.setFont (font (10.0f));
            g.drawText (s.on ? "ON" : "OFF: BYPASSED, ITS SETTINGS KEPT", t, juce::Justification::centredLeft, false);
        }
        const char* keyNames[] = { "ON", "CHANGE", "REMOVE", "CLOSE" };
        const juce::TextButton* keyButtons[] = { &onKey, &changeKey, &removeKey, &closeKey };
        for (int i = 0; i < 4; ++i)
        {
            const auto k = keyButtons[i]->getBounds().toFloat();
            stamped (g, keyNames[i], k.translated (0.0f, k.getHeight() + 2.0f).withHeight (12.0f).expanded (22.0f, 0.0f), 8.0f);
        }
        printed (g, "SETTINGS", knobsArea().withHeight (12.0f).translated (0.0f, -18.0f));
        const auto& params = mixerfx::paramsOf (s.type);
        for (int i = 0; i < (int) knobs.size(); ++i)
        {
            const auto k = knobs[(size_t) i]->getBounds().toFloat();
            const auto isOutput = i == (int) params.size();
            const auto name = isOutput ? juce::String ("OUTPUT") : params[(size_t) i].name;
            const auto value = isOutput ? dbText (s.outputDb, -999.0f) : mixerfx::valueText (s.type, i, s.values[(size_t) i]);
            if (isOutput)   // (beside it, not under: the keys' row is short)
            {
                stamped (g, "OUTPUT", k.withX (k.getRight() + 6.0f).withWidth (70.0f).withHeight (12.0f).translated (0.0f, 6.0f), 8.0f,
                         juce::Justification::centredLeft);
                valueScreen (g, k.withX (k.getRight() + 6.0f).withWidth (70.0f).withHeight (18.0f).translated (0.0f, 22.0f), value);
                continue;
            }
            stamped (g, name.toUpperCase(), k.translated (0.0f, k.getHeight() + 2.0f).withHeight (11.0f).expanded (34.0f, 0.0f), 8.0f);
            valueScreen (g, k.withY (k.getBottom() + 14.0f).withHeight (18.0f).withSizeKeepingCentre (100.0f, 18.0f), value);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        LedDisplay::paintGlassOverlay (g, screen().reduced (3.0f), screenSize);
        Finish::paintAs (std::nullopt);
    }

private:
    mixer::FxSlot current() const { return page.settingsOf (strip).fx[(size_t) juce::jlimit (0, mixer::numFxSlots - 1, slot)]; }
    void edit (const std::function<void (mixer::FxSlot&)>& change, bool final = true)
    {
        auto s = page.settingsOf (strip);
        change (s.fx[(size_t) slot]);
        page.apply (strip, s, final);
        onKey.setToggleState (s.fx[(size_t) slot].on, juce::dontSendNotification);
        repaint();
    }

    MixerPage& page;
    int strip = 0, slot = 0, type = 0;
    juce::TextButton onKey, changeKey, removeKey, closeKey;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    juce::Image satin;
};

//==============================================================================
MixerPage::MixerPage (ParagonX1AudioProcessor& p) : processor (p)
{
    editor = std::make_unique<EffectEditor> (*this);
    addChildComponent (*editor);
    rebuild();
    startTimerHz (30);
}

MixerPage::~MixerPage() = default;

juce::String MixerPage::getStripName (int i) const
{
    if (i >= 0 && i < parts.size())
        return library::partLabel (parts[i]).toUpperCase();
    return "SECTION";
}

mixer::StripSettings MixerPage::settingsOf (int strip) const
{
    return strip < parts.size() ? processor.getStrip (parts[strip]) : processor.getSectionStrip();
}

void MixerPage::apply (int strip, const mixer::StripSettings& s, bool final)
{
    if (strip < parts.size())
        processor.setStrip (parts[strip], s, final);
    else
        processor.setSectionStrip (s, final);
    shownVersion = processor.getMixerVersion();   // (our own change: nothing to show again)
}

void MixerPage::chooseEffect (int strip, int slot, int type)
{
    if (strip < 0 || strip >= (int) strips.size() || slot < 0 || slot >= mixer::numFxSlots)
        return;
    auto s = settingsOf (strip);
    s.fx[(size_t) slot] = type > 0 ? mixerfx::defaultSlot (type) : mixer::FxSlot {};
    apply (strip, s, true);
    strips[(size_t) strip]->show (s);
    if (editor->isVisible() && editor->getStripIndex() == strip && editor->getSlotIndex() == slot)
        type > 0 ? editor->rebuild() : closeEditor();
}

void MixerPage::showSlotMenu (int strip, int slot)
{
    juce::PopupMenu menu;
    const auto current = settingsOf (strip).fx[(size_t) slot].type;
    menu.addSectionHeader ("SLOT " + juce::String (slot + 1) + ": AN EFFECT");
    for (int t = 1; t < mixerfx::typeNames.size(); ++t)
        menu.addItem (t, mixerfx::typeNames[t], true, t == current);
    if (current > 0)
    {
        menu.addSeparator();
        menu.addItem (1000, "Remove");
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (strips[(size_t) strip]->slots[slot]),
                        [safe = juce::Component::SafePointer<MixerPage> (this), strip, slot] (int chosen)
                        {
                            if (safe == nullptr || chosen == 0)
                                return;
                            safe->chooseEffect (strip, slot, chosen == 1000 ? 0 : chosen);
                            if (chosen != 1000)
                                safe->openEditor (strip, slot);
                        });
}

void MixerPage::openEditor (int strip, int slot)
{
    if (strip < 0 || strip >= (int) strips.size() || ! settingsOf (strip).fx[(size_t) slot].isLoaded())
        return;
    editor->open (strip, slot);
}

void MixerPage::closeEditor() { editor->setVisible (false); }

juce::TextButton& MixerPage::getKey (int strip, Key k)
{
    auto& s = *strips[(size_t) strip];
    return k == Key::mute ? s.mute : k == Key::solo ? s.solo : s.invert;
}
juce::TextButton& MixerPage::getEffectKey (int strip, int slot) { return strips[(size_t) strip]->fxKeys[slot]; }
juce::Slider& MixerPage::getPanKnob (int strip) { return strips[(size_t) strip]->pan; }
void MixerPage::moveFader (int strip, float db) { strips[(size_t) strip]->fader.onChange (db, true); }
juce::Rectangle<int> MixerPage::getStripBounds (int strip) const { return strips[(size_t) strip]->getBounds(); }
int MixerPage::getEditorKnobCount() const { return editor->getNumKnobs(); }
juce::Slider& MixerPage::getEditorKnob (int i) { return editor->getKnob (i); }
bool MixerPage::isEditorOpen() const { return editor->isVisible(); }

void MixerPage::rebuild()
{
    closeEditor();
    strips.clear();
    parts.clear();
    for (const auto& part : processor.getStageParts())
        parts.add (part.name);
    for (int i = 0; i <= parts.size(); ++i)
    {
        const auto isSection = i == parts.size();
        if (isSection && parts.isEmpty())
            break;   // nothing loaded: no strips
        auto strip = std::make_unique<Strip> (*this, i, isSection);
        addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
    editor->toFront (false);
    refresh();
    resized();
}

void MixerPage::refresh()
{
    juce::StringArray now;   // (other parts: the strips again first)
    for (const auto& part : processor.getStageParts())
        now.add (part.name);
    if (now != parts)
    {
        rebuild();   // (which comes back here with the parts as they are)
        return;
    }
    shownVersion = processor.getMixerVersion();
    const auto section = processor.getSection();
    for (int i = 0; i < (int) strips.size(); ++i)
    {
        auto& s = *strips[(size_t) i];
        s.show (settingsOf (i));
        if (i < parts.size())
        {
            juce::String keys;
            if (section.has_value() && i < (int) section->parts.size())
                keys = "KEYS " + notenames::name (section->parts[(size_t) i].low) + " TO " + notenames::name (section->parts[(size_t) i].high);
            s.setName (getStripName (i), keys, "PART " + juce::String (i + 1));
        }
        else
            s.setName ("SECTION", juce::String (parts.size()) + (parts.size() == 1 ? " PART" : " PARTS") + ", AFTER THE STAGE", "THE WHOLE INSTRUMENT");
    }
    if (editor->isVisible())
        editor->show();
}

void MixerPage::timerCallback()
{
    // The parts changed (another instrument, a split): the strips again. Their settings changed elsewhere (an undo, a
    // preset): shown again. Every tick: the meters, where each goes.
    juce::StringArray now;
    for (const auto& part : processor.getStageParts())
        now.add (part.name);
    if (now != parts)
        rebuild();
    else if (processor.getMixerVersion() != shownVersion)
        refresh();
    const auto stageOn = processor.getParameters().getRawParameterValue (ParamIDs::stageOn)->load() > 0.5f;
    const auto insert = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::instInsert)->load());
    for (int i = 0; i < (int) strips.size(); ++i)
    {
        auto& s = *strips[(size_t) i];
        const auto isSection = i == parts.size();
        s.meter (processor.takeStripPeak (isSection ? mixer::sectionStrip : i, 0), processor.takeStripPeak (isSection ? mixer::sectionStrip : i, 1));
        s.setOutput (! isSection ? (stageOn ? juce::String ("STAGE") : juce::String ("SECTION"))
                     : insert > 0 ? "FX BUS " + juce::String::charToString ((juce::juce_wchar) ('A' + insert - 1)) : juce::String ("MAIN (FX PAGE)"));
    }
}

void MixerPage::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    DockStyle::drawTitle (g, getLocalBounds().reduced (16, 0).withHeight (32),
                          "THE MIXER: A STRIP FOR EACH PART, BEFORE THE STAGE; THE SECTION'S AFTER IT");
    if (strips.empty())
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (font (14.0f, juce::Font::plain));
        g.drawText ("NO INSTRUMENT: LOAD ONE (OR A SECTION) ON THE INSTRUMENT PAGE, AND EACH PART GETS A STRIP HERE",
                    getLocalBounds(), juce::Justification::centred, false);
    }
}

void MixerPage::resized()
{
    stripsArea = getLocalBounds().withTrimmedTop (titleHeight).reduced (8, 4);
    if (! strips.empty())
    {
        auto a = stripsArea;
        const auto w = juce::jmin (300, a.getWidth() / (int) strips.size());
        for (auto& s : strips)
            s->setBounds (a.removeFromLeft (w));
    }
    editor->setBounds (stripsArea);
}
