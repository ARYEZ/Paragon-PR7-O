#include "InstrumentPage.h"

#include "NoteNames.h"
#include "ui/DockPanels.h"
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
    poll.callback = [this]
    {
        const auto now = processor.getProgram();
        const auto octave = notenames::middleCOctave.load();
        const auto chosen = processor.getKeyswitch();
        if (now != shown || octave != shownOctave || chosen != shownSwitch)
        {
            shown = now;
            shownOctave = octave;
            shownSwitch = chosen;
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

void InstrumentKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                        juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);
    if (switches (note) && ! isDown)
        markSwitch (g, note, area.withTrimmedTop (area.getHeight() * 0.62f).reduced (1.0f, 0.0f));
    else if (plays (note) && ! isDown)
    {
        const auto band = area.withTrimmedTop (area.getHeight() * 0.62f).reduced (1.0f, 0.0f);
        ParagonColours::setFill (g, ParagonColours::accent.withAlpha (0.32f), band);
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
        ParagonColours::setFill (g, ParagonColours::accent.withAlpha (0.6f), band);
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
InstrumentPage::InstrumentPage (ParagonX1AudioProcessor& p) : processor (p), map (p)
{
    auto& state = processor.getParameters();
    loadButton.setTooltip ("Load an SFZ instrument (or one audio file, played across the keyboard from C3/C4). You can also drop one on this page.");
    loadButton.onClick = [this] { chooseFile(); };
    clearButton.setTooltip ("Unload the instrument (one undo step)");
    clearButton.onClick = [this] { processor.clearInstrument(); };
    nameLabel.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, ParagonColours::text);
    fileLabel.setFont (juce::FontOptions (12.5f));
    fileLabel.setColour (juce::Label::textColourId, ParagonColours::textDim);
    info.setMultiLine (true);
    info.setReadOnly (true);
    info.setScrollbarsShown (true);
    info.setCaretVisible (false);
    info.setFont (juce::FontOptions (13.5f));
    info.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    info.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    info.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    info.setColour (juce::TextEditor::textColourId, ParagonColours::text);
    for (auto* c : std::initializer_list<juce::Component*> { &loadButton, &clearButton, &nameLabel, &fileLabel, &info, &map })
        addAndMakeVisible (c);

    const auto add = [this, &state] (std::vector<std::unique_ptr<ParamControl>>& list, const char* id, const char* label, const char* tip)
    {
        list.push_back (std::make_unique<ParamControl> (state, id, label, tip));
        addAndMakeVisible (*list.back());
    };
    add (playControls, ParamIDs::instLevel, "LEVEL", "The instrument's level, after its own (each region's volume and velocity)");
    add (playControls, ParamIDs::instPan, "PAN", "A balance, after each region's own pan");
    add (playControls, ParamIDs::instTranspose, "TRANSPOSE", "Every note this many semitones higher or lower (the samples pitched, not other samples chosen)");
    add (playControls, ParamIDs::instTune, "TUNE", "Fine tuning, in cents (100 to a semitone)");
    add (playControls, ParamIDs::instRelease, "RELEASE", "The instrument's release times (how long a note takes to fade once let go) times this");
    add (playControls, ParamIDs::instVoices, "POLYPHONY", "How many notes can sound at once; past it one fades out in 10 ms: the oldest let go, "
                                                          "else the oldest held that isn't the chord's lowest or highest");
    add (playControls, ParamIDs::instDynamics, "DYNAMICS", "VELOCITY: how hard you play picks the recording (as the file says). MOD WHEEL: "
                                                           "a held note (sustain, tremolo) starts every dynamic at once and the mod wheel "
                                                           "crossfades between them, soft to loud, as you hold it; shorts still go by velocity");
    add (playControls, ParamIDs::instAutoSustain, "AUTO SUSTAIN", "A held note longer than its recording keeps going: it crossfades back into the "
                                                                  "steady part of the recording before the bow comes off (our own; for files "
                                                                  "without loops, as VSCO 2's)");
    add (sendControls, ParamIDs::instSendDelay, "SEND DELAY", "How much of the instrument goes to the DELAY (FX page: switch it on there)");
    add (sendControls, ParamIDs::instSendReverb, "SEND REVERB", "How much of the instrument goes to the REVERB (FX page: switch it on there)");
    add (sendControls, ParamIDs::instInsert, "INSERT BUS", "Play it through an insert bus's DRIVE, GLITCH, MOD and LO-FI (FX page, INSERTS). "
                                                          "On a bus, the bus's own sends are used instead of these");

    processor.instrumentBroadcaster.addChangeListener (this);
    refresh();
    startTimerHz (4);
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
    return files.size() == 1 && isInstrumentFile (juce::File (files[0]));
}

void InstrumentPage::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() == 1 && isInstrumentFile (juce::File (files[0])))
        processor.loadInstrument (juce::File (files[0]));
}

void InstrumentPage::chooseFile()
{
    const auto current = juce::File (processor.getInstrumentFile());
    chooser = std::make_unique<juce::FileChooser> ("Load an instrument (SFZ, or one audio file)",
                                                   current.existsAsFile() ? current.getParentDirectory() : juce::File(),
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
    shownSwitch = -2;   // lit again by the timer
    if (shown == nullptr)
        return;
    for (const auto& a : shown->articulations)
    {
        auto button = std::make_unique<juce::TextButton> (articulationName (a).toUpperCase());
        button->setClickingTogglesState (false);
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
        button->setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
        button->setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        button->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        button->setTooltip ("Switch to it (as its keyswitch key, " + notenames::name (a.lo) + ", does)");
        const auto key = a.lo;
        button->onClick = [this, key] { processor.chooseKeyswitch (key); };
        addAndMakeVisible (*button);
        articulationButtons.push_back (std::move (button));
    }
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
    clearButton.setEnabled (shownFile.isNotEmpty() || shown != nullptr);

    const juce::File file (shownFile);
    nameLabel.setText (shown != nullptr ? shown->name.toUpperCase()
                       : shownFile.isNotEmpty() ? file.getFileNameWithoutExtension().toUpperCase()
                       : juce::String ("NO INSTRUMENT"), juce::dontSendNotification);
    fileLabel.setText (shownFile.isNotEmpty() ? shownFile : juce::String ("Load an SFZ file, or drop one here"), juce::dontSendNotification);

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
        text << "MEMORY    " << juce::String ((double) shown->bytes / (1024.0 * 1024.0), 1) << " MB (whole samples, in memory)\n";
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
    for (const auto& area : { headArea, infoArea, mapArea, playArea, sendArea })
        DockStyle::drawPanel (g, area.toFloat());
    DockStyle::drawTitle (g, infoArea.reduced (16, 0).withHeight (32), "THE INSTRUMENT");
    DockStyle::drawTitle (g, mapArea.reduced (16, 0).withHeight (32), "ITS REGIONS: KEYS ACROSS, VELOCITY UP");
    DockStyle::drawTitle (g, playArea.reduced (16, 0).withHeight (32), "HOW IT PLAYS");
    DockStyle::drawTitle (g, sendArea.reduced (16, 0).withHeight (32), "WHERE IT GOES");
}

void InstrumentPage::resized()
{
    auto area = getLocalBounds();
    headArea = area.removeFromTop (78);
    area.removeFromTop (10);
    infoArea = area.removeFromLeft (440);
    {
        auto inside = infoArea.reduced (14, 0).withTrimmedTop (36);
        articulationArea = inside.removeFromTop (articulationButtons.empty() ? 0 : 30 * (int) articulationButtons.size() + 12);
        auto rows = articulationArea;
        for (auto& b : articulationButtons)
            b->setBounds (rows.removeFromTop (30).reduced (0, 2));
    }
    area.removeFromLeft (10);
    auto bottom = area.removeFromBottom (150);
    area.removeFromBottom (10);
    mapArea = area;
    playArea = bottom.removeFromLeft (bottom.getWidth() * 72 / 100);
    bottom.removeFromLeft (10);
    sendArea = bottom;

    auto head = headArea.reduced (18, 14);
    loadButton.setBounds (head.removeFromLeft (130).withSizeKeepingCentre (130, 40));
    head.removeFromLeft (10);
    clearButton.setBounds (head.removeFromLeft (100).withSizeKeepingCentre (100, 40));
    head.removeFromLeft (24);
    nameLabel.setBounds (head.removeFromTop (30));
    fileLabel.setBounds (head);

    info.setBounds (infoArea.reduced (14, 0).withTrimmedTop (36 + articulationArea.getHeight()).withTrimmedBottom (12));
    map.setBounds (mapArea.withTrimmedTop (30).reduced (6, 4));

    const auto row = [] (juce::Rectangle<int> panel) { return panel.withTrimmedTop (36).withTrimmedBottom (14).reduced (10, 0); };
    std::vector<juce::Component*> play, sends;
    for (auto& c : playControls)
        play.push_back (c.get());
    for (auto& c : sendControls)
        sends.push_back (c.get());
    SynthPanel::layoutRow (row (playArea), play, 0, 6);
    SynthPanel::layoutRow (row (sendArea), sends, 0, 6);
}
