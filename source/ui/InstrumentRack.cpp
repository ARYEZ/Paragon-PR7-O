#include "InstrumentRack.h"

#include "ui/Finish.h"
#include "ui/LedDisplay.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/PedalBody.h"

namespace
{
    // As PR7-I's GuitarRack (and PR7-G's rack units): the chassis, the room round it for its light, the screens' size.
    const PedalBody::Shape unitShape { 16.0f, 10.0f, 5.0f, 0.3f };
    constexpr float glowRoom = 7.0f;
    constexpr float screenSize = 0.45f;
    constexpr float sectionGap = 28.0f;    // between the sections, a printed red line in its middle
    constexpr float badgeWidth = 190.0f, displayWidth = 300.0f, sideWidth = 200.0f;
    constexpr float titleHeight = 24.0f;   // the page's name, over it
    constexpr float screenHeight = 108.0f;
    const auto orange = LedDisplay::ledOrange;

    juce::FontOptions font (float height, int style = juce::Font::bold) { return juce::FontOptions (height, style); }

    void stamped (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                  juce::Justification justification = juce::Justification::centred)
    {
        g.setFont (font (height));
        ParagonLookAndFeel::drawStampedText (g, text, area, justification, Finish::ink());
    }

    // A red bracket printed over keys with a word in its middle (as PR7-G's HOLD TO TUNER, PR7-I's KEY MAP).
    void bracket (juce::Graphics& g, juce::Rectangle<float> a, juce::Rectangle<float> b, const juce::String& word)
    {
        const auto y = a.getY() - 8.0f;
        g.setColour (Finish::printedRed());
        g.setFont (font (8.0f));
        const auto label = juce::Rectangle<float> (a.getCentreX(), y - 6.0f, b.getCentreX() - a.getCentreX(), 12.0f);
        g.drawText (word, label, juce::Justification::centred, false);
        const auto gap = juce::GlyphArrangement::getStringWidth (font (8.0f), word) * 0.5f + 6.0f;
        g.fillRect (juce::Rectangle<float> (a.getCentreX(), y, label.getWidth() * 0.5f - gap, 1.2f));
        g.fillRect (juce::Rectangle<float> (label.getCentreX() + gap, y, label.getWidth() * 0.5f - gap, 1.2f));
        g.fillRect (juce::Rectangle<float> (a.getCentreX(), y, 1.2f, 5.0f));
        g.fillRect (juce::Rectangle<float> (b.getCentreX() - 1.2f, y, 1.2f, 5.0f));
    }
}

InstrumentRack::InstrumentRack()
{
    setOpaque (false);
    for (auto* key : { &playingKey, &routingKey, &stageKey, &loadKey, &sectionKey, &clearKey })
    {
        key->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);   // a small key, its stripe lit when on
        addAndMakeVisible (*key);
    }
    playingKey.setComponentID ("rackPlaying");
    routingKey.setComponentID ("rackRouting");
    playingKey.setTooltip ("PLAYING: how the instrument plays (LEVEL ... POLYPHONY, GLIDE, DYNAMICS, AUTO SUSTAIN, LEGATO)");
    routingKey.setTooltip ("ROUTING: where it goes (SEND DELAY, SEND REVERB, INSERT BUS)");
    playingKey.onClick = [this] { setPage (Page::playing); };
    routingKey.onClick = [this] { setPage (Page::routing); };
    stageKey.setComponentID ("rackStage");
    stageKey.setTooltip ("STAGE: where it sits (MIC, ROOM LEVEL, DECAY, SPREAD, the ROOM, STAGE on); drag the parts on the STAGE picture");
    stageKey.onClick = [this] { setPage (Page::stage); };
    setPage (Page::playing);
}

void InstrumentRack::resized()
{
    satin = Finish::makeSatinGrain (juce::jmax (1, getWidth() * 2), juce::jmax (1, getHeight() * 2), 1234);
    playingKey.setBounds (pageSwitchCell (0));
    routingKey.setBounds (pageSwitchCell (1));
    stageKey.setBounds (pageSwitchCell (2));
    loadKey.setBounds (instrumentKeyCell (0));
    sectionKey.setBounds (instrumentKeyCell (1));
    clearKey.setBounds (instrumentKeyCell (2));
}

void InstrumentRack::setScreen (const Screen& s)
{
    if (s == screen)
        return;
    screen = s;
    repaint (screenArea());
}

void InstrumentRack::setPage (Page newPage)
{
    playingKey.setToggleState (newPage == Page::playing, juce::dontSendNotification);
    routingKey.setToggleState (newPage == Page::routing, juce::dontSendNotification);
    stageKey.setToggleState (newPage == Page::stage, juce::dontSendNotification);
    if (newPage == page)
        return;
    page = newPage;
    repaint();
    if (onPageChanged != nullptr)
        onPageChanged();
}

//==============================================================================
juce::Rectangle<float> InstrumentRack::face() const
{
    return getLocalBounds().toFloat().reduced (glowRoom).withTrimmedBottom (unitShape.frontEdge).reduced (9.0f);
}

juce::Rectangle<float> InstrumentRack::content() const { return face().reduced (12.0f, 8.0f); }
juce::Rectangle<float> InstrumentRack::badgeColumn() const { return content().withWidth (badgeWidth); }
juce::Rectangle<float> InstrumentRack::displayColumn() const { return content().withTrimmedLeft (badgeWidth + sectionGap).withWidth (displayWidth); }

juce::Rectangle<float> InstrumentRack::pageArea() const
{
    auto area = content();
    area.removeFromLeft (badgeWidth + sectionGap + displayWidth + sectionGap);
    area.removeFromTop (titleHeight + 6.0f);
    return area;
}

juce::Rectangle<float> InstrumentRack::sideOfPage() const { return pageArea().removeFromRight (sideWidth); }

juce::Rectangle<int> InstrumentRack::pageSwitchCell (int index) const
{
    auto area = badgeColumn();
    area = area.removeFromBottom (58.0f).withTrimmedBottom (14.0f);   // names stamped under them
    const auto width = area.getWidth() / 3.0f;   // (0.8.0: three)
    return area.withX (area.getX() + width * (float) index).withWidth (width).withSizeKeepingCentre (48.0f, 26.0f).toNearestInt();
}

juce::Rectangle<int> InstrumentRack::screenArea() const { return displayColumn().withHeight (screenHeight).toNearestInt(); }

juce::Rectangle<int> InstrumentRack::instrumentKeyCell (int index) const
{
    auto area = displayColumn().withTrimmedTop (screenHeight + 16.0f);   // INSTRUMENT printed over them
    const auto width = area.getWidth() / 3.0f;
    return area.withX (area.getX() + width * (float) index).withWidth (width).removeFromTop (28.0f)
               .withSizeKeepingCentre (54.0f, 26.0f).toNearestInt();
}

juce::Rectangle<int> InstrumentRack::knobCell (int index) const
{
    auto area = pageArea();
    area.removeFromRight (sideWidth + sectionGap);
    const auto width = area.getWidth() / (float) numKnobs;
    const auto height = juce::jmin (area.getHeight(), 136.0f);
    return juce::Rectangle<float> (area.getX() + width * (float) index, area.getCentreY() - height * 0.5f, width, height)
        .reduced (4.0f, 0.0f).toNearestInt();
}

juce::Rectangle<int> InstrumentRack::routingKnobCell (int index) const
{
    auto area = pageArea();
    area.removeFromRight (sideWidth + sectionGap);
    const auto width = juce::jmin (area.getWidth() / (float) numKnobs, 150.0f);
    const auto height = juce::jmin (area.getHeight(), 136.0f);
    return juce::Rectangle<float> (area.getX() + width * (float) index, area.getCentreY() - height * 0.5f, width, height)
        .reduced (4.0f, 0.0f).toNearestInt();
}

juce::Rectangle<int> InstrumentRack::menuCell (int) const
{
    const auto area = sideOfPage();
    return juce::Rectangle<float> (area.getX(), area.getY() + 18.0f, area.getWidth(), 30.0f).toNearestInt();
}

juce::Rectangle<int> InstrumentRack::pageKeyCell (int index) const
{
    // Two keys side by side under the menu (0.7.0: AUTO SUSTAIN, LEGATO).
    const auto area = sideOfPage();
    const auto half = area.getWidth() * 0.5f;
    return juce::Rectangle<float> (area.getX() + half * (float) juce::jlimit (0, 1, index), area.getY() + 74.0f, half, 28.0f)
        .withSizeKeepingCentre (54.0f, 26.0f).toNearestInt();
}

//==============================================================================
void InstrumentRack::paint (juce::Graphics& g)
{
    // The unit, and everything on it (its knobs and keys too, till paintOverChildren), in pearl black.
    Finish::paintAs (Finish::Type::pearlBlack);

    const auto chassis = getLocalBounds().toFloat().reduced (glowRoom);
    PedalBody::paintUnit (g, chassis, face(), [this] (juce::Graphics& gg) { gg.drawImage (satin, getLocalBounds().toFloat()); }, unitShape);

    // The badge: PARAGON over the PR7-O mark, what it is printed in red; the PAGE keys under it.
    PedalBody::paintBadge (g, badgeColumn().withHeight (82.0f).translated (2.0f, 2.0f), "ORCHESTRA INSTRUMENT");
    {
        const auto a = pageSwitchCell (0).toFloat(), b = pageSwitchCell (1).toFloat(), c = pageSwitchCell (2).toFloat();
        bracket (g, a, c, "PAGE");
        stamped (g, "PLAYING", a.translated (0.0f, a.getHeight() + 2.0f).withHeight (12.0f).expanded (8.0f, 0.0f), 7.5f);
        stamped (g, "ROUTING", b.translated (0.0f, b.getHeight() + 2.0f).withHeight (12.0f).expanded (8.0f, 0.0f), 7.5f);
        stamped (g, "STAGE", c.translated (0.0f, c.getHeight() + 2.0f).withHeight (12.0f).expanded (8.0f, 0.0f), 7.5f);
    }

    // The printed lines between the sections.
    const auto printedLine = [&] (float x)
    {
        g.setColour (Finish::printedRed().withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, face().getY() + 14.0f, 1.5f, face().getHeight() - 28.0f));
    };
    printedLine (displayColumn().getX() - sectionGap * 0.5f);
    printedLine (displayColumn().getRight() + sectionGap * 0.5f);
    printedLine (sideOfPage().getX() - sectionGap * 0.5f);

    paintScreen (g);

    // The INSTRUMENT keys' names stamped under them, INSTRUMENT printed over them.
    const char* names[] = { "LOAD", "SECTION", "CLEAR" };
    for (int i = 0; i < 3; ++i)
    {
        const auto key = instrumentKeyCell (i).toFloat();
        stamped (g, names[i], key.translated (0.0f, key.getHeight() + 2.0f).withHeight (12.0f).expanded (20.0f, 0.0f), 8.0f);
    }
    bracket (g, instrumentKeyCell (0).toFloat(), instrumentKeyCell (2).toFloat(), "INSTRUMENT");

    // The page's name, over it.
    auto title = pageArea().withY (content().getY()).withHeight (titleHeight);
    g.setFont (font (15.0f, juce::Font::bold | juce::Font::italic));
    ParagonLookAndFeel::drawStampedText (g, page == Page::playing ? "PLAYING" : page == Page::routing ? "ROUTING" : "STAGE",
                                         title.removeFromLeft (110.0f), juce::Justification::centredLeft, Finish::ink());
    g.setColour (Finish::printedRed());
    g.setFont (font (8.0f));
    g.drawText (page == Page::playing ? "HOW THE INSTRUMENT PLAYS" : page == Page::routing ? "WHERE IT GOES: THE SENDS AND AN INSERT BUS"
                                                                                            : "WHERE IT SITS: ITS SEATS, THE MICS AND THE ROOM",
                title, juce::Justification::centredLeft, false);

    // The page's right-hand column: its menu's name and screen (the menu sits on it); PLAYING's AUTO SUSTAIN key's name.
    const auto menu = menuCell (0).toFloat();
    stamped (g, page == Page::playing ? "DYNAMICS" : page == Page::routing ? "INSERT BUS" : "ROOM",
             menu.withHeight (12.0f).translated (0.0f, -15.0f), 8.0f, juce::Justification::centredLeft);
    LedDisplay::paintScreen (g, menu, screenSize);
    if (page != Page::routing)
    {
        for (int i = 0; i < 2; ++i)
        {
            const auto key = pageKeyCell (i).toFloat();
            const auto* name = page == Page::playing ? (i == 0 ? "AUTO SUSTAIN" : "LEGATO") : (i == 0 ? "STAGE" : "RESET SEATS");
            stamped (g, name, key.translated (0.0f, key.getHeight() + 2.0f).withHeight (12.0f).expanded (22.0f, 0.0f), 8.0f);
        }
    }
}

void InstrumentRack::paintScreen (juce::Graphics& g)
{
    // As PR7-I's: a caption, the name big (something short in the corner), a rule, two lines.
    const auto glass = LedDisplay::paintScreen (g, screenArea().toFloat(), screenSize);
    auto text = glass.reduced (12.0f, 8.0f);
    auto captionRow = text.removeFromTop (11.0f);
    g.setColour (orange.withAlpha (0.6f));
    g.setFont (font (8.5f));
    g.drawText (screen.caption, captionRow, juce::Justification::centredLeft, false);
    text.removeFromTop (3.0f);
    auto big = text.removeFromTop (26.0f);
    g.setColour (orange);
    g.setFont (font (12.0f));
    if (screen.corner.isNotEmpty())
        g.drawText (screen.corner, big.removeFromRight (64.0f), juce::Justification::centredRight, false);
    g.setFont (font (19.0f));
    g.drawFittedText (screen.name, big.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    text.removeFromTop (3.0f);
    g.setColour (orange.withAlpha (0.25f));
    g.fillRect (text.removeFromTop (1.0f));
    text.removeFromTop (5.0f);
    g.setColour (orange);
    g.setFont (font (11.5f));
    g.drawFittedText (screen.line1, text.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    g.setColour (orange.withAlpha (0.65f));
    g.setFont (font (9.5f));
    g.drawFittedText (screen.line2, text.removeFromTop (15.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    LedDisplay::paintGlassOverlay (g, glass, screenSize);
}

void InstrumentRack::paintOverChildren (juce::Graphics& g)
{
    // The glass over the menu's screen (its text sits behind it, as on any of the unit's screens).
    LedDisplay::paintGlassOverlay (g, menuCell (0).toFloat().reduced (3.0f), screenSize);
    Finish::paintAs (std::nullopt);
}
