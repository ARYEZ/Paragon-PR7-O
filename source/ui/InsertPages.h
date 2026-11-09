#pragma once

#include "PluginProcessor.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/Glitch.h"
#include "ui/SynthControls.h"

// THE INSERT BUSES' own pieces (stage 7d): the bus strip (which bus, whether the instrument plays into it, its sends), and
// DRIVE's and GLITCH's pages. (A bus's MOD and LO-FI use ModFxPage.)

// What DRIVE does now, worked out by running a copy of it (the same model and settings) on a 120 Hz sine at half
// level: one cycle in (dim) and out (bright), and the harmonics it adds, in dB below the note. Remade when a setting
// changes (on the message thread: a fraction of a second of audio).
class DrivePreview final : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::Timer
{
public:
    DrivePreview (ParagonX1AudioProcessor&, int bus);
    void paint (juce::Graphics&) override;

    static constexpr double hz = 120.0, sampleRate = 48000.0;
    static constexpr float level = 0.5f;
    // The last render's cycle in and out (400 samples each), and harmonics 1 to 12 (dB, the note at 0). For tests too.
    const std::vector<float>& getCycleIn() const noexcept { return cycleIn; }
    const std::vector<float>& getCycleOut() const noexcept { return cycleOut; }
    const std::array<float, 12>& getHarmonicsDb() const noexcept { return harmonicsDb; }
    void render();

private:
    void timerCallback() override;
    ParagonX1AudioProcessor& processor;
    int bus;
    juce::String shownKey;
    std::vector<float> cycleIn, cycleOut;
    std::array<float, 12> harmonicsDb {};
};

// What GLITCH does now, from a copy of it (same mode and settings, its fixed seed) on a plain riff: eighth-note plucks
// at the song's tempo, two bars. The level in (dim) and out (bright) over time, with the beats marked.
class GlitchPreview final : public juce::Component,
                            public juce::SettableTooltipClient,
                            private juce::Timer
{
public:
    GlitchPreview (ParagonX1AudioProcessor&, int bus);
    void paint (juce::Graphics&) override;
    void render();
    const std::vector<float>& getLevelsIn() const noexcept { return levelsIn; }    // per 5 ms
    const std::vector<float>& getLevelsOut() const noexcept { return levelsOut; }
    double getSeconds() const noexcept { return seconds; }

private:
    void timerCallback() override;
    ParagonX1AudioProcessor& processor;
    int bus;
    juce::String shownKey;
    std::vector<float> levelsIn, levelsOut;
    double seconds = 4.0, bpm = 120.0;
};

// DRIVE's or GLITCH's page on a bus: ON and MODEL / MODE along the top with BACK TO RACK, its preview, and the
// controls that model or mode uses (as on PR7-G's panel: TONE but not on HALO or ABYSS, ABYSS's EQ, SKYLINE's ATTACK
// and GATE; GLITCH's LENGTH as a time or a note by SYNC).
class InsertEffectPage final : public juce::Component,
                               private juce::Timer
{
public:
    enum class Kind { drive, glitch };
    InsertEffectPage (ParagonX1AudioProcessor&, Kind, int bus);
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Component& getDisplay() noexcept { return *display; }
    std::vector<ParamControl*> getShownControls();
    Kind getKind() const noexcept { return kind; }

private:
    void timerCallback() override;
    bool shows (const ParamControl&) const;

    ParagonX1AudioProcessor& processor;
    Kind kind;
    int bus;
    std::unique_ptr<juce::Component> display;
    std::vector<std::unique_ptr<ParamControl>> top, controls;
    juce::TextButton back { "BACK TO RACK" };
    juce::String layoutKey;
};

// The INSERTS view's strip: the bus (A to D), whether the instrument plays into it (lit; click to route it here, or back
// to none), and the bus's DELAY and REVERB sends.
class InsertBusStrip final : public juce::Component,
                             private juce::Timer
{
public:
    explicit InsertBusStrip (ParagonX1AudioProcessor&);
    std::function<void (int bus)> onBusChanged;

    void setBus (int);
    int getBus() const noexcept { return bus; }
    juce::TextButton& getBusButton (int b) noexcept { return busButtons[(size_t) b]; }
    juce::TextButton& getRouteButton() noexcept { return routeButton; }
    ParamControl& getSend (int s) noexcept { return *sends[(size_t) s]; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    ParagonX1AudioProcessor& processor;
    int bus = 0;
    std::array<juce::TextButton, ParamIDs::numInsertBuses> busButtons;
    juce::TextButton routeButton;
    std::array<std::unique_ptr<ParamControl>, 2> sends;
};
