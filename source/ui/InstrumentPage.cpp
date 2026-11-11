#include "InstrumentPage.h"

#include "NoteNames.h"
#include "ui/DockPanels.h"
#include "ui/Finish.h"
#include "library/Library.h"
#include "ui/LedDisplay.h"
#include "ui/ParagonLookAndFeel.h"

//==============================================================================
InstrumentKeyboard::InstrumentKeyboard (ParagonX1AudioProcessor& p)
    : juce::MidiKeyboardComponent (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard), processor (p)
{
    // PR7-D's keyboard colours (before 0.41.0): pearl keys, onyx sharps, lit red-orange as they're played.
    setColour (whiteNoteColourId, juce::Colour (0xffdcd7da));
    setColour (blackNoteColourId, juce::Colour (0xff141115));
    setColour (keySeparatorLineColourId, juce::Colour (0xff6b6569));
    setColour (mouseOverKeyOverlayColourId, ParagonColours::glow.withAlpha (0.25f));
    setColour (keyDownOverlayColourId, ParagonColours::glow.withAlpha (0.85f));
    setColour (shadowColourId, juce::Colours::black.withAlpha (0.5f));
    setColour (upDownButtonBackgroundColourId, juce::Colour (0xff151217));
    setColour (upDownButtonArrowColourId, ParagonColours::text);
    setColour (textLabelColourId, ParagonColours::inkDim);
    setAvailableRange (lowestKey, highestKey);
    setOctaveForMiddleC (notenames::middleCOctave.load());
    setWantsKeyboardFocus (false);   // computer-key playing would fight the DAW's own shortcuts
    setScrollButtonsVisible (false); // all 88 keys fit
    setVelocity (0.8f, true);        // higher on the key plays softer
    partOfKey.fill (-1);
    poll.callback = [this]
    {
        const auto now = processor.getProgram();
        const auto octave = notenames::middleCOctave.load();
        const auto chosen = processor.getKeyswitch();
        const auto source = processor.getInstrumentFile();
        if (now != shown || octave != shownOctave || chosen != shownSwitch || source != shownSource)
        {
            shown = now;
            shownOctave = octave;
            shownSwitch = chosen;
            shownSource = source;
            partOfKey.fill (-1);
            if (const auto section = sampler::section::fromText (source))
                for (int i = 0; i < (int) section->parts.size(); ++i)
                    for (int k = section->parts[(size_t) i].low; k <= section->parts[(size_t) i].high; ++k)
                        partOfKey[(size_t) k] = i;
            setOctaveForMiddleC (octave);
            repaint();
        }
    };
    poll.startTimerHz (4);
}

void InstrumentKeyboard::layOut()
{
    setKeyWidth ((float) getWidth() / 52.0f);   // 88 keys: 52 white
    setLowestVisibleKey (lowestKey);
}

void InstrumentKeyboard::paint (juce::Graphics& g)
{
    shown = processor.getProgram();
    juce::MidiKeyboardComponent::paint (g);
}

float InstrumentKeyboard::tintOf (int note) const noexcept
{
    // A section's parts in turn full and faint, so its splits show.
    const auto part = note >= 0 && note < 128 ? partOfKey[(size_t) note] : -1;
    return part >= 0 && part % 2 == 1 ? 0.4f : 1.0f;
}

void InstrumentKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                        juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);
    if (switches (note) && ! isDown)
        markSwitch (g, note, area.withTrimmedTop (area.getHeight() * 0.62f).reduced (1.0f, 0.0f));
    else if (plays (note) && ! isDown)
    {
        const auto band = area.withTrimmedTop (area.getHeight() * 0.62f).reduced (1.0f, 0.0f);
        ParagonColours::setFill (g, ParagonColours::accent.withAlpha (0.32f * tintOf (note)), band);
        g.fillRect (band);
    }
}

void InstrumentKeyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                        juce::Colour noteFillColour)
{
    juce::MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, noteFillColour);
    if (switches (note) && ! isDown)
        markSwitch (g, note, area.withTrimmedTop (area.getHeight() * 0.7f).reduced (area.getWidth() * 0.2f, 2.0f));
    else if (plays (note) && ! isDown)
    {
        const auto band = area.withTrimmedTop (area.getHeight() * 0.7f).reduced (area.getWidth() * 0.2f, 2.0f);
        ParagonColours::setFill (g, ParagonColours::accent.withAlpha (0.6f * tintOf (note)), band);
        g.fillRect (band);
    }
}

void InstrumentKeyboard::markSwitch (juce::Graphics& g, int note, juce::Rectangle<float> band)
{
    // A keyswitch: the family's orange, the chosen one full, the others faint.
    g.setColour (ParagonColours::familyOrange.withAlpha (note == shownSwitch ? 0.95f : 0.4f));
    g.fillRect (band);
}

//==============================================================================
RegionMap::RegionMap (ParagonX1AudioProcessor& p) : processor (p)
{
    setOpaque (false);
    startTimerHz (15);
}

void RegionMap::setProgram (std::shared_ptr<const sampler::Program> p)
{
    program = std::move (p);
    shownSwitch = processor.getKeyswitch();
    rebuild();
}

void RegionMap::rebuild()
{
    boxes.clear();
    if (program != nullptr)
    {
        // One box per place (keys x velocities): its round robins counted, not drawn on top of each other. Only the
        // articulation chosen (and what every articulation plays).
        std::map<std::array<int, 4>, int> places;
        for (const auto& r : program->regions)
            if (r.playsUnder (shownSwitch))
                ++places[{ r.loKey, r.hiKey, r.loVel, r.hiVel }];
        for (const auto& [place, count] : places)
            boxes.push_back ({ place[0], place[1], place[2], place[3], count });
    }
    firstKey = 21;
    lastKey = 108;
    if (program != nullptr && program->lowestKey() >= 0)
    {
        firstKey = juce::jmin (firstKey, program->lowestKey());
        lastKey = juce::jmax (lastKey, program->highestKey());
    }
    repaint();
}

void RegionMap::timerCallback()
{
    if (const auto chosen = processor.getKeyswitch(); chosen != shownSwitch)
    {
        shownSwitch = chosen;
        rebuild();
    }
    std::array<bool, 128> now {};
    for (int k = 0; k < 128; ++k)
        now[(size_t) k] = processor.getSampler().isKeyHeld (k);
    if (now != lit)
    {
        lit = now;
        repaint();
    }
}

juce::Rectangle<float> RegionMap::boxFor (int loKey, int hiKey, int loVel, int hiVel) const
{
    const auto plot = getLocalBounds().toFloat().reduced (12.0f, 10.0f).withTrimmedBottom (18.0f);
    const auto keyWidth = plot.getWidth() / (float) (lastKey - firstKey + 1);
    const auto y = [&plot] (int vel) { return plot.getBottom() - plot.getHeight() * (float) vel / 127.0f; };
    const auto x = [&] (int key) { return plot.getX() + keyWidth * (float) (juce::jlimit (firstKey, lastKey + 1, key) - firstKey); };
    return juce::Rectangle<float>::leftTopRightBottom (x (loKey), y (hiVel), x (hiKey + 1), y (juce::jmax (0, loVel - 1)));
}

void RegionMap::paint (juce::Graphics& g)
{
    const auto plot = boxFor (firstKey, lastKey, 1, 127);
    // The grid: a line at each C (named under it) and at velocities 32, 64, 96.
    g.setColour (juce::Colour (0xff100d11));
    g.fillRect (plot);
    for (int c = (firstKey + 11) / 12 * 12; c <= lastKey; c += 12)
    {
        const auto x = boxFor (c, c, 1, 127).getX();
        g.setColour (ParagonColours::panelEdge);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (notenames::name (c), juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred);
    }
    for (const auto vel : { 32, 64, 96 })
    {
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (boxFor (0, 0, vel + 1, vel).getBottom()), plot.getX(), plot.getRight());
    }
    // The keys sounding now: a lit column.
    for (int k = firstKey; k <= lastKey; ++k)
        if (lit[(size_t) k])
        {
            const auto column = boxFor (k, k, 1, 127);
            ParagonColours::setFill (g, ParagonColours::accent.withAlpha (0.22f), column, true);
            g.fillRect (column);
        }
    // The regions.
    for (const auto& b : boxes)
    {
        const auto box = boxFor (b.loKey, b.hiKey, b.loVel, b.hiVel).reduced (0.5f);
        const auto colour = ParagonColours::velocityColour ((float) b.hiVel / 127.0f);
        g.setColour (colour.withAlpha (0.28f));
        g.fillRect (box);
        g.setColour (colour.withAlpha (0.9f));
        g.drawRect (box, 1.0f);
        if (b.count > 1 && box.getWidth() > 14.0f && box.getHeight() > 12.0f)
        {
            g.setColour (ParagonColours::text.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("x" + juce::String (b.count), box, juce::Justification::centred, false);
        }
    }
    if (boxes.empty())
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("NO INSTRUMENT: ITS REGIONS SHOW HERE (KEYS ACROSS, VELOCITY UP)", plot, juce::Justification::centred);
    }
}

//==============================================================================
juce::String articulationName (const sampler::Program::Articulation& a)
{
    const auto sfzName = juce::MidiMessage::getMidiNoteName (a.lo, true, true, 4);   // SFZ's: c4 = 60
    auto label = a.label.trim();
    if (label.startsWithIgnoreCase (sfzName + " ") || label.equalsIgnoreCase (sfzName))
        label = notenames::name (a.lo) + label.substring (sfzName.length());
    return label;
}

//==============================================================================
InstrumentPage::InstrumentPage (ParagonX1AudioProcessor& p) : processor (p), map (p), picture (p), stageView (p)
{
    auto& state = processor.getParameters();
    rack.loadKey.setTooltip ("LOAD: an SFZ instrument (or one audio file, played across the keyboard from C3/C4). You can also drop one on this page.");
    rack.loadKey.onClick = [this] { showLibrary (! browser.isVisible()); };   // (0.11.0: the library browser)
    rack.sectionKey.setTooltip ("SECTION: several SFZ instruments split over the keyboard as one (a string section: basses, cellos, violas, "
                                "violins), with one set of keyswitches. Choose their files; PR7-O splits the keys where each next one's "
                                "range begins (move the splits under SPLITS). You can also drop several .sfz files on this page.");
    rack.sectionKey.onClick = [this] { chooseSection(); };
    rack.clearKey.setTooltip ("CLEAR: unload the instrument (one undo step)");
    rack.clearKey.onClick = [this] { processor.clearInstrument(); };
    info.setMultiLine (true);
    info.setReadOnly (true);
    info.setScrollbarsShown (true);
    info.setCaretVisible (false);
    info.setFont (juce::FontOptions (13.5f));
    info.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    info.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    info.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    info.setColour (juce::TextEditor::textColourId, ParagonColours::text);
    for (auto* c : std::initializer_list<juce::Component*> { &rack, &info, &map, &picture })
        addAndMakeVisible (c);
    addChildComponent (stageView);
    addChildComponent (browser);
    browser.onLoad = [this] (const juce::File& file) { processor.loadInstrument (file); showLibrary (false); };
    browser.onLoadSection = [this] (const juce::StringArray& files)
    {
        if (const auto section = sampler::section::fromFiles (files); ! section.parts.empty())
            processor.loadSection (section);
        showLibrary (false);
    };
    browser.onOpenFile = [this] { showLibrary (false); chooseFile(); };
    browser.onClose = [this] { showLibrary (false); };
    browser.setDownload (&processor.getDownload());   // 0.13.0: GET VSCO 2 CE / SONATINA
    browser.onGet = [this] (const downloads::Library& l) { processor.getLibrary (l); };

    // The INSTRUMENT / REGIONS switch over the picture panel.
    for (auto* b : { &showPicture, &showStage, &showRegions })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
        b->setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim);
        b->setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        b->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        addChildComponent (*b);
    }
    showPicture.setTooltip ("The instrument itself, its notes lit as they play");
    showRegions.setTooltip ("Its regions: where each recording plays (keys across, velocity up)");
    showStage.setTooltip ("The stage: where each part sits, heard from there through the mics in front (switch STAGE on, on the rack's STAGE page). Drag a part to move it");
    showPicture.onClick = [this] { chooseRegions (false); };
    showStage.onClick = [this] { chooseStage(); };
    showRegions.onClick = [this] { chooseRegions (true); };

    // The rack's knobs and menus, in its pearl black.
    Finish::paintAs (Finish::Type::pearlBlack);
    const auto add = [this, &state] (std::vector<std::unique_ptr<ParamControl>>& list, const char* id, const char* label, const char* tip)
    {
        list.push_back (std::make_unique<ParamControl> (state, id, label, tip));
        list.back()->setRack (true);
        rack.addAndMakeVisible (*list.back());
    };
    add (playControls, ParamIDs::instLevel, "LEVEL", "The instrument's level, after its own (each region's volume and velocity)");
    add (playControls, ParamIDs::instPan, "PAN", "A balance, after each region's own pan");
    add (playControls, ParamIDs::instTranspose, "TRANSPOSE", "Every note this many semitones higher or lower (the samples pitched, not other samples chosen)");
    add (playControls, ParamIDs::instTune, "TUNE", "Fine tuning, in cents (100 to a semitone)");
    add (playControls, ParamIDs::instRelease, "RELEASE", "The instrument's release times (how long a note takes to fade once let go) times this");
    add (playControls, ParamIDs::instVoices, "POLYPHONY", "How many notes can sound at once; past it one fades out in 10 ms: the oldest let go, "
                                                          "else the oldest held that isn't the chord's lowest or highest");
    add (playControls, ParamIDs::instGlide, "GLIDE", "With LEGATO: a joined note slides in pitch from the note before over this time "
                                                     "(a string player's shift). Off: it moves straight to its pitch");
    add (sendControls, ParamIDs::instSendDelay, "SEND DELAY", "How much of the instrument goes to the DELAY (FX page: switch it on there)");
    add (stageControls, ParamIDs::stageMic, "MIC", "How far in front of the stage the pair of mics stands: close, the parts clear and dry; far, more of the room");
    add (stageControls, ParamIDs::stageRoomLevel, "ROOM LEVEL", "The room's reflections and tail, louder or quieter than they'd be");
    add (stageControls, ParamIDs::stageDecay, "DECAY", "How long the room's tail rings, times its own");
    add (stageControls, ParamIDs::stageSpread, "SPREAD", "How wide the seats spread across the stage: 0, all in the middle; 100%, as placed; more, wider");
    add (ensembleControls, ParamIDs::ensPlayers, "PLAYERS", "How many players each note has: x1 as recorded; more, copies of it a little out of tune, a little late and spread across, for a bigger section (as loud as before)");
    add (ensembleControls, ParamIDs::ensDetune, "DETUNE", "How far out of tune the extra players are, at most (cents: 100 to a semitone)");
    add (ensembleControls, ParamIDs::ensTiming, "TIMING", "How late the extra players come in, at most (never early: the note you play stays on time)");
    add (ensembleControls, ParamIDs::ensDoubleLevel, "DOUBLE LEVEL", "How loud the doubled octaves are, against the note played");
    add (sendControls, ParamIDs::instSendReverb, "SEND REVERB", "How much of the instrument goes to the REVERB (FX page: switch it on there)");
    const auto menu = [this, &state] (const char* id, const char* label, const char* tip)
    {
        auto control = std::make_unique<ParamControl> (state, id, label, tip);
        control->setCompact (true);
        control->setRack (true);
        rack.addAndMakeVisible (*control);
        return control;
    };
    dynamicsMenu = menu (ParamIDs::instDynamics, "DYNAMICS", "VELOCITY: how hard you play picks the recording (as the file says). MOD WHEEL: "
                                                             "a held note (sustain, tremolo) starts every dynamic at once and the mod wheel "
                                                             "crossfades between them, soft to loud, as you hold it; shorts still go by velocity");
    doubleMenu = menu (ParamIDs::ensDouble, "DOUBLE", "Every note also an octave up, an octave down, or both, as orchestrators double a line; in a section, played by whichever part has that key");
    roomMenu = menu (ParamIDs::stageRoom, "ROOM", "The room the stage is in: a studio (small, quick), a scoring stage, a concert hall, a cathedral (big, long)");
    insertMenu = menu (ParamIDs::instInsert, "INSERT BUS", "Play it through an insert bus's DRIVE, GLITCH, MOD and LO-FI (FX page, INSERTS). "
                                                           "On a bus, the bus's own sends are used instead of these");
    Finish::paintAs (std::nullopt);

    // AUTO SUSTAIN: a key on the rack, its stripe lit while it's on.
    autoSustainKey.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
    autoSustainKey.setTooltip ("AUTO SUSTAIN: a held note longer than its recording keeps going: it crossfades back into the steady part "
                               "of the recording before the bow comes off (our own; for files without loops, as VSCO 2's)");
    autoSustainAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (ParamIDs::instAutoSustain), [this] (float v)
    {
        autoSustainKey.setToggleState (v > 0.5f, juce::dontSendNotification);
    });
    autoSustainKey.onClick = [this] { autoSustainAttachment->setValueAsCompleteGesture (autoSustainKey.getToggleState() ? 0.0f : 1.0f); };
    autoSustainAttachment->sendInitialUpdate();
    rack.addAndMakeVisible (autoSustainKey);

    // LEGATO (0.7.0): a key beside it.
    legatoKey.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
    legatoKey.setTooltip ("LEGATO: a note played while you still hold a sustained one joins it: the old note fades as the new one "
                          "fades in, started past its attack, so the line flows instead of starting again (faster playing, a "
                          "shorter join). Let go of the new note and it goes back to the one still held. Chords (keys pressed "
                          "together), shorts and notes over an octave apart aren't joined. Our own: the open libraries have no "
                          "recorded transitions");
    legatoAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (ParamIDs::instLegato), [this] (float v)
    {
        legatoKey.setToggleState (v > 0.5f, juce::dontSendNotification);
    });
    legatoKey.onClick = [this] { legatoAttachment->setValueAsCompleteGesture (legatoKey.getToggleState() ? 0.0f : 1.0f); };
    legatoAttachment->sendInitialUpdate();
    rack.addAndMakeVisible (legatoKey);
    // THE STAGE's keys (0.8.0): STAGE on, RESET SEATS.
    for (auto* key : { &stageOnKey, &resetSeatsKey })
    {
        key->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
        rack.addAndMakeVisible (*key);
    }
    stageOnKey.setTooltip ("STAGE: each part heard from its seat on the stage through a pair of mics, with the room's reflections and tail, "
                           "so parts recorded apart sit in one space (our own; off: they play as recorded)");
    stageOnAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (ParamIDs::stageOn), [this] (float v)
    {
        stageOnKey.setToggleState (v > 0.5f, juce::dontSendNotification);
        stageView.repaint();
    });
    stageOnKey.onClick = [this] { stageOnAttachment->setValueAsCompleteGesture (stageOnKey.getToggleState() ? 0.0f : 1.0f); };
    stageOnAttachment->sendInitialUpdate();
    resetSeatsKey.setTooltip ("RESET SEATS: every part back to its usual seat");
    resetSeatsKey.onClick = [this] { processor.resetSeats(); stageView.refresh(); };
    rack.onPageChanged = [this] { showRackPage(); };
    showRackPage();

    processor.instrumentBroadcaster.addChangeListener (this);
    refresh();
    startTimerHz (4);
}

void InstrumentPage::showRackPage()
{
    const auto page = rack.getPage();
    const auto playing = page == InstrumentRack::Page::playing, routing = page == InstrumentRack::Page::routing,
               onStage = page == InstrumentRack::Page::stage, ensemble = page == InstrumentRack::Page::ensemble;
    for (auto& c : playControls)
        c->setVisible (playing);
    dynamicsMenu->setVisible (playing);
    autoSustainKey.setVisible (playing);
    legatoKey.setVisible (playing);
    for (auto& c : sendControls)
        c->setVisible (routing);
    insertMenu->setVisible (routing);
    for (auto& c : stageControls)
        c->setVisible (onStage);
    roomMenu->setVisible (onStage);
    stageOnKey.setVisible (onStage);
    resetSeatsKey.setVisible (onStage);
    for (auto& c : ensembleControls)
        c->setVisible (ensemble);
    doubleMenu->setVisible (ensemble);
    const auto switches = page == InstrumentRack::Page::articulations;   // 0.14.0
    for (auto& b : articulationButtons)
        b->setVisible (switches);
    for (size_t i = 0; i < splitRows.size(); ++i)
    {
        auto& row = *splitRows[i];
        row.name.setVisible (switches);
        row.keys.setVisible (switches);
        row.down.setVisible (switches && i > 0);
        row.up.setVisible (switches && i > 0);
    }
}

void InstrumentPage::chooseRegions (bool regions)
{
    regionsChosen = regions;
    stageChosen = false;
    showView();
}

void InstrumentPage::chooseStage()
{
    stageChosen = true;
    showView();
}

void InstrumentPage::showView()
{
    // INSTRUMENT (when there's a picture), STAGE (0.8.0: when something's loaded), REGIONS.
    const auto hasPicture = picture.getMode() != OrchestraView::Mode::none;
    const auto hasStage = shown != nullptr || shownFile.isNotEmpty();
    const auto onStage = hasStage && stageChosen;
    const auto showing = hasPicture && ! regionsChosen && ! onStage;
    picture.setVisible (showing);
    stageView.setVisible (onStage);
    map.setVisible (! showing && ! onStage);
    info.setVisible (map.isVisible());   // 0.14.0: what the instrument holds, beside its regions
    showPicture.setVisible (hasPicture);
    showStage.setVisible (hasStage);
    showRegions.setVisible (hasPicture || hasStage);
    showPicture.setToggleState (showing, juce::dontSendNotification);
    showStage.setToggleState (onStage, juce::dontSendNotification);
    showRegions.setToggleState (! showing && ! onStage, juce::dontSendNotification);
    repaint (mapArea);
}

void InstrumentPage::updateScreen()
{
    InstrumentRack::Screen screen;
    const auto section = sampler::section::fromText (shownFile);
    if (shownLoading)
        screen.caption = "LOADING  " + juce::String (juce::roundToInt (processor.getLoadProgress() * 100.0)) + "%";
    else
        screen.caption = section.has_value() ? "THE SECTION" : "THE INSTRUMENT";
    juce::StringArray partLabels;   // (0.14.0: a section's parts without their "-KS")
    if (section.has_value())
        for (const auto& part : section->parts)
            partLabels.add (library::partLabel (sampler::section::nameOf (part)));
    screen.name = shown != nullptr && section.has_value() ? partLabels.joinIntoString (" + ").toUpperCase()
                : shown != nullptr ? shown->name.toUpperCase()
                : shownFile.isNotEmpty() ? sampler::section::describe (shownFile).toUpperCase()
                : juce::String ("NO INSTRUMENT");
    if (shown != nullptr)
    {
        if (const auto a = shown->articulationOf (processor.getKeyswitch()); a >= 0)
        {
            const auto& articulation = shown->articulations[(size_t) a];
            screen.corner = notenames::name (articulation.lo);
            auto name = articulationName (articulation);
            if (name.startsWith (screen.corner + " "))
                name = name.substring (screen.corner.length() + 1);
            screen.line1 = name.toUpperCase();
        }
        else if (shown->lowestKey() >= 0)
            screen.line1 = "KEYS " + notenames::name (shown->lowestKey()) + " TO " + notenames::name (shown->highestKey());
    }
    else if (shownFile.isNotEmpty() && ! shownLoading)
        screen.line1 = "COULDN'T LOAD IT";
    else if (shownFile.isEmpty())
        screen.line1 = "LOAD AN SFZ, OR DROP ONE ON THE PAGE";
    screen.line2 = section.has_value() ? juce::String ((int) section->parts.size()) + " PARTS SPLIT OVER THE KEYS, FROM "
                                             + sampler::section::folderOf (shownFile).getFullPathName()
                 : shown == nullptr && shownFile.isNotEmpty() && ! shownLoading ? processor.getLoadProblem()
                 : shownFile;
    rack.setScreen (screen);
}

InstrumentPage::~InstrumentPage()
{
    processor.instrumentBroadcaster.removeChangeListener (this);
}

bool InstrumentPage::isInstrumentFile (const juce::File& f)
{
    return f.hasFileExtension ("sfz;wav;aif;aiff;flac;ogg");
}

bool InstrumentPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.size() == 1)
        return isInstrumentFile (juce::File (files[0]));
    for (const auto& f : files)   // 0.4.0: several SFZ: a section
        if (! juce::File (f).hasFileExtension ("sfz"))
            return false;
    return files.size() > 1;
}

void InstrumentPage::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() == 1 && isInstrumentFile (juce::File (files[0])))
        processor.loadInstrument (juce::File (files[0]));
    else if (files.size() > 1 && isInterestedInFileDrag (files))
        processor.loadSection (sampler::section::fromFiles (files));
}

void InstrumentPage::chooseSection()
{
    const auto start = sampler::section::folderOf (processor.getInstrumentFile());
    chooser = std::make_unique<juce::FileChooser> ("A section: choose its SFZ files (basses, cellos, violas, violins...)",
                                                   start.isDirectory() ? start : juce::File(), "*.sfz");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& c)
                          {
                              juce::StringArray files;
                              for (const auto& f : c.getResults())
                                  if (f.existsAsFile())
                                      files.add (f.getFullPathName());
                              if (files.size() == 1)
                                  processor.loadInstrument (juce::File (files[0]));   // one: just that instrument
                              else if (files.size() > 1)
                                  processor.loadSection (sampler::section::fromFiles (files));
                          });
}

void InstrumentPage::rebuildSplits()
{
    splitRows.clear();
    const auto section = editedSection.has_value() ? editedSection : processor.getSection();
    if (section.has_value())
        for (int i = 0; i < (int) section->parts.size(); ++i)
        {
            // 0.14.0: on the rack's ARTICULATIONS page: the part's name and keys on a small screen, - / + keys under it.
            auto row = std::make_unique<SplitRow>();
            row->name.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            row->name.setColour (juce::Label::textColourId, LedDisplay::ledOrange.withAlpha (0.7f));
            row->keys.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            row->keys.setColour (juce::Label::textColourId, LedDisplay::ledOrange);
            for (auto* l : { &row->name, &row->keys })
            {
                l->setJustificationType (juce::Justification::centred);
                l->setBorderSize ({});
                l->setMinimumHorizontalScale (0.6f);
                l->setInterceptsMouseClicks (false, false);
                rack.addChildComponent (*l);
            }
            if (i > 0)
            {
                for (auto* b : { &row->down, &row->up })
                {
                    b->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
                    rack.addChildComponent (*b);
                }
                const auto below = sampler::section::nameOf (section->parts[(size_t) i - 1]);
                const auto above = sampler::section::nameOf (section->parts[(size_t) i]);
                row->down.setTooltip ("Move the split between " + below + " and " + above + " down a semitone (" + above + " plays one key more)");
                row->up.setTooltip ("Move the split between " + below + " and " + above + " up a semitone (" + below + " plays one key more)");
                row->down.onClick = [this, i] { moveSplit (i, -1); };
                row->up.onClick = [this, i] { moveSplit (i, 1); };
            }
            splitRows.push_back (std::move (row));
        }
    rack.setNumSplits ((int) splitRows.size());
    showSplits();
    showRackPage();
    resized();
}

void InstrumentPage::showSplits()
{
    const auto section = editedSection.has_value() ? editedSection : processor.getSection();
    if (! section.has_value() || section->parts.size() != splitRows.size())
        return;
    for (size_t i = 0; i < splitRows.size(); ++i)
    {
        const auto& part = section->parts[i];
        splitRows[i]->name.setText (library::partLabel (sampler::section::nameOf (part)).toUpperCase(), juce::dontSendNotification);
        splitRows[i]->keys.setText (notenames::name (part.low) + " to " + notenames::name (part.high), juce::dontSendNotification);
    }
}

juce::String InstrumentPage::getSplitRowText (int part) const
{
    if (part < 0 || part >= (int) splitRows.size())
        return {};
    return splitRows[(size_t) part]->name.getText() + " " + splitRows[(size_t) part]->keys.getText();
}

bool InstrumentPage::moveSplit (int part, int semitones)
{
    auto section = editedSection.has_value() ? editedSection : processor.getSection();
    if (! section.has_value() || part < 1 || part >= (int) section->parts.size())
        return false;
    auto& below = section->parts[(size_t) part - 1];
    auto& above = section->parts[(size_t) part];
    const auto low = above.low + semitones;
    if (low <= below.low || low > above.high)   // each keeps a key at least
        return false;
    above.low = low;
    below.high = low - 1;
    editedSection = section;
    editedAt = juce::Time::getMillisecondCounterHiRes();
    showSplits();
    return true;
}

void InstrumentPage::applyPendingSplits()
{
    if (editedSection.has_value())
    {
        const auto section = *editedSection;
        editedSection.reset();
        if (const auto now = processor.getSection(); ! now.has_value() || ! (*now == section))
            processor.loadSection (section);
    }
}

void InstrumentPage::showLibrary (bool show)
{
    if (show)
    {
        browser.setFolders (library::folders(), false);
        if (const auto file = juce::File (processor.getInstrumentFile()); juce::File::isAbsolutePath (processor.getInstrumentFile()) && file.existsAsFile())
            browser.chooseFile (file);
        browser.toFront (false);
    }
    browser.setVisible (show);
    rack.loadKey.setToggleState (show, juce::dontSendNotification);
}

void InstrumentPage::chooseFile()
{
    const auto folder = sampler::section::folderOf (processor.getInstrumentFile());
    chooser = std::make_unique<juce::FileChooser> ("Load an instrument (SFZ, or one audio file)",
                                                   folder.isDirectory() ? folder : juce::File(),
                                                   "*.sfz;*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& c)
                          {
                              if (const auto file = c.getResult(); file.existsAsFile())
                                  processor.loadInstrument (file);
                          });
}

void InstrumentPage::timerCallback()
{
    if (editedSection.has_value() && juce::Time::getMillisecondCounterHiRes() - editedAt > 300.0)
        applyPendingSplits();
    updateScreen();
    if (processor.isLoadingInstrument() != shownLoading)
        refresh();
    if (notenames::middleCOctave.load() != shownOctave)
    {
        shownOctave = notenames::middleCOctave.load();
        rebuildArticulations();   // (their names follow NOTE NAMES)
    }
    if (const auto chosen = processor.getKeyswitch(); chosen != shownSwitch && shown != nullptr)
    {
        shownSwitch = chosen;
        const auto lit = shown->articulationOf (chosen);
        for (int i = 0; i < (int) articulationButtons.size(); ++i)
            articulationButtons[(size_t) i]->setToggleState (i == lit, juce::dontSendNotification);
        updateInfo();
    }
}

void InstrumentPage::rebuildArticulations()
{
    articulationButtons.clear();
    rack.setArticulationNames ({});
    shownSwitch = -2;   // lit again by the timer
    if (shown == nullptr)
        return;
    // 0.14.0: a key on the rack's ARTICULATIONS page each (the owner: "the keyswitch box ... can become a 2nd page in
    // the rack unit"), its stripe lit when chosen, its name stamped under it by the rack.
    juce::StringArray names;
    for (const auto& a : shown->articulations)
    {
        auto button = std::make_unique<juce::TextButton>();
        button->setName (articulationName (a).toUpperCase());
        button->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
        button->setClickingTogglesState (false);
        button->setTooltip (articulationName (a) + ": switch to it (as its keyswitch key, " + notenames::name (a.lo) + ", does)");
        const auto key = a.lo;
        button->onClick = [this, key] { processor.chooseKeyswitch (key); };
        rack.addChildComponent (*button);
        names.add (button->getName());
        articulationButtons.push_back (std::move (button));
    }
    rack.setArticulationNames (names);
    showRackPage();
    resized();
    timerCallback();
}

void InstrumentPage::refresh()
{
    shown = processor.getProgram();
    shownFile = processor.getInstrumentFile();
    shownLoading = processor.isLoadingInstrument();
    map.setProgram (shown);
    rebuildArticulations();
    rack.clearKey.setEnabled (shownFile.isNotEmpty() || shown != nullptr);
    picture.refresh();
    stageView.refresh();
    showView();
    updateScreen();
    rebuildSplits();

    updateInfo();
    repaint();
}

void InstrumentPage::updateInfo()
{
    const auto keyswitch = shown != nullptr && ! shown->articulations.empty() ? processor.getKeyswitch() : sampler::Program::anyKeyswitch;
    juce::String text;
    if (shownLoading)
        text << "LOADING...\n\nIts samples are read into memory; a big instrument takes a while.\n";
    else if (shown == nullptr)
    {
        if (shownFile.isNotEmpty())
            text << "COULDN'T LOAD IT\n" << processor.getLoadProblem() << "\n\n"
                 << "The preset keeps where it was: put the library back there (or load it again) to hear it.\n";
        else
            text << "PR7-O plays SFZ instruments: a text file listing its samples and where each plays. Open orchestral "
                    "libraries come this way (VSCO 2 Community Edition, Sonatina Symphonic Orchestra, Virtual Playing "
                    "Orchestra), as do many others sold as plain WAV + SFZ.\n\n"
                    "LOAD... an .sfz (or one audio file, played across the keys from C3/C4), or drop one on this page.\n";
    }
    else
    {
        const auto lo = shown->lowestKey(), hi = shown->highestKey();
        const auto middle = lo >= 0 ? (lo + hi) / 2 : 60;
        // A key in the middle of its range that plays (some instruments leave gaps).
        auto probe = middle;
        for (int d = 0; d < 64 && ! shown->plays (probe); ++d)
            probe = shown->plays (middle + d) ? middle + d : middle - d;
        text << "REGIONS   " << (int) shown->regions.size() << " (from " << shown->numFiles << (shown->numFiles == 1 ? " file)" : " files)") << "\n";
        if (lo >= 0)
            text << "KEYS      " << notenames::name (lo) << " to " << notenames::name (hi) << "\n"
                 << "AT " << notenames::name (probe) << "     " << shown->velocityLayersAt (probe, keyswitch) << " velocity layer"
                 << (shown->velocityLayersAt (probe, keyswitch) == 1 ? "" : "s") << ", " << shown->roundRobinsAt (probe, keyswitch)
                 << " round robin" << (shown->roundRobinsAt (probe, keyswitch) == 1 ? "" : "s")
                 << (keyswitch != sampler::Program::anyKeyswitch && shown->articulationOf (keyswitch) >= 0
                         ? " (" + articulationName (shown->articulations[(size_t) shown->articulationOf (keyswitch)]).toLowerCase() + ")"
                         : juce::String())
                 << "\n";
        // 0.11.0: what's in memory, and the rest read from disk as it plays.
        const auto mb = [] (size_t bytes) { return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB"; };
        if (shown->wholeBytes > shown->bytes)
            text << "MEMORY    " << mb (shown->bytes) << " in memory, " << mb (shown->wholeBytes - shown->bytes)
                 << " streamed from disk as it plays\n";
        else
            text << "MEMORY    " << mb (shown->bytes) << " (whole samples, in memory)\n";
        auto heldRegions = 0, autoLoops = 0, fileLoops = 0;
        for (const auto& r : shown->regions)
        {
            heldRegions += r.held ? 1 : 0;
            autoLoops += r.autoLoop ? 1 : 0;
            fileLoops += r.loop != sampler::Region::Loop::none ? 1 : 0;
        }
        text << "HELD      " << heldRegions << " of its regions sustain (MOD WHEEL dynamics work on these)\n";
        if (fileLoops > 0)
            text << "LOOPS     " << fileLoops << " regions loop as the file says\n";
        if (autoLoops > 0)
            text << "AUTO SUSTAIN  " << autoLoops << " regions can go on past their recording\n";
        if (! shown->unread.isEmpty())
        {
            text << "\nNOT PLAYED YET (later versions: loops and crossfades 0.2.0, keyswitches 0.4.0...):\n";
            for (const auto& u : shown->unread)
                text << "  " << u << "\n";
        }
        if (! shown->problems.isEmpty())
        {
            text << "\nPROBLEMS:\n";
            for (int i = 0; i < juce::jmin (40, shown->problems.size()); ++i)
                text << "  " << shown->problems[i] << "\n";
            if (shown->problems.size() > 40)
                text << "  ... and " << (shown->problems.size() - 40) << " more\n";
        }
    }
    info.setText (text, false);
}

void InstrumentPage::paint (juce::Graphics& g)
{
    // 0.14.0: one panel the page's width (the owner: "with [the keyswitch box] gone from the main screen we can stretch
    // out the tron display of our stage and use all the space").
    DockStyle::drawPanel (g, mapArea.toFloat());
}

void InstrumentPage::paintOverChildren (juce::Graphics& g)
{
    if (browser.isVisible())   // (0.11.0: the library browser covers the panels)
        return;
    // The picture panel's title, over the picture.
    juce::String title = "ITS REGIONS: KEYS ACROSS, VELOCITY UP";
    if (stageView.isVisible())
        title = "THE STAGE: DRAG A PART TO ITS SEAT";
    else if (picture.isVisible())
        title = picture.getMode() == OrchestraView::Mode::section ? "THE SECTION: EACH PART LIT AS IT PLAYS"
              : picture.getKind() == orchestra::Kind::trombone ? juce::String ("THE TROMBONE: EACH NOTE'S SLIDE POSITION LIT")
              : orchestra::isBrass (picture.getKind()) ? "THE " + orchestra::nameOf (picture.getKind()).toUpperCase() + ": EACH NOTE'S VALVES LIT"
              : orchestra::isWoodwind (picture.getKind()) ? "THE " + orchestra::displayNameOf (picture.getKind()).toUpperCase() + ": EACH NOTE'S FINGERING LIT"
              : "THE " + orchestra::nameOf (picture.getKind()).toUpperCase() + ": EACH NOTE LIT WHERE IT'S PLAYED";
    DockStyle::drawTitle (g, mapArea.reduced (16, 0).withHeight (32), title);
}

void InstrumentPage::resized()
{
    auto area = getLocalBounds();
    rack.setBounds (area.removeFromTop (InstrumentRack::preferredHeight));
    area.removeFromTop (4);
    browser.setBounds (area);   // (0.11.0: over the panels below the rack)
    mapArea = area;

    // 0.14.0: the articulations' keys and the splits' screens on the rack's ARTICULATIONS page.
    for (size_t i = 0; i < articulationButtons.size(); ++i)
        articulationButtons[i]->setBounds (rack.articulationCell ((int) i));
    for (size_t i = 0; i < splitRows.size(); ++i)
    {
        auto screen = rack.splitScreenCell ((int) i).reduced (6, 3);
        splitRows[i]->name.setBounds (screen.removeFromTop (13));
        splitRows[i]->keys.setBounds (screen);
        splitRows[i]->down.setBounds (rack.splitKeyCell ((int) i, false));
        splitRows[i]->up.setBounds (rack.splitKeyCell ((int) i, true));
    }

    // REGIONS: what the instrument holds in a column at the left, its regions beside it.
    auto regions = mapArea.withTrimmedTop (30).reduced (6, 4);
    info.setBounds (regions.removeFromLeft (juce::jmin (420, regions.getWidth() / 3)).reduced (8, 4));
    map.setBounds (regions);
    picture.setBounds (mapArea.reduced (2));
    stageView.setBounds (mapArea.reduced (2));
    auto switches = mapArea.reduced (14, 0).withHeight (32).withSizeKeepingCentre (mapArea.getWidth() - 28, 22);
    showRegions.setBounds (switches.removeFromRight (90));
    switches.removeFromRight (4);
    showStage.setBounds (switches.removeFromRight (78));   // (0.8.0)
    switches.removeFromRight (4);
    showPicture.setBounds (switches.removeFromRight (104));

    // The rack's controls in its cells.
    for (int i = 0; i < (int) playControls.size(); ++i)
        playControls[(size_t) i]->setBounds (rack.knobCell (i));
    dynamicsMenu->setBounds (rack.menuCell (0).reduced (4, 2));
    autoSustainKey.setBounds (rack.pageKeyCell (0));
    legatoKey.setBounds (rack.pageKeyCell (1));
    for (int i = 0; i < (int) sendControls.size(); ++i)
        sendControls[(size_t) i]->setBounds (rack.routingKnobCell (i));
    insertMenu->setBounds (rack.menuCell (0).reduced (4, 2));
    for (int i = 0; i < (int) stageControls.size(); ++i)
        stageControls[(size_t) i]->setBounds (rack.routingKnobCell (i));
    roomMenu->setBounds (rack.menuCell (0).reduced (4, 2));
    for (int i = 0; i < (int) ensembleControls.size(); ++i)
        ensembleControls[(size_t) i]->setBounds (rack.routingKnobCell (i));
    doubleMenu->setBounds (rack.menuCell (0).reduced (4, 2));
    stageOnKey.setBounds (rack.pageKeyCell (0));
    resetSeatsKey.setBounds (rack.pageKeyCell (1));
}
