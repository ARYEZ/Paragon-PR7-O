#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"

// THE SENDS' pages (stage 7b): DELAY's and REVERB's big pages (their units' EDIT), and the send matrix on the FX
// page's SENDS view.

// DELAY's echoes on a timeline: each repeat where and how loud it comes (MIX, then FEEDBACK each time), up to 2.5 s;
// with PING-PONG the left's above the line and the right's below it. Follows the settings (and the song's tempo).
class EchoDisplay final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer
{
public:
    explicit EchoDisplay (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;

    // The echoes drawn: time (seconds), level (0-1), and the side (0 both, -1 left, 1 right). For the tests.
    struct Echo { float seconds, level; int side; };
    std::vector<Echo> echoes() const;
    static constexpr float span = 2.5f;   // seconds shown
    static constexpr float floorDb = -48.0f;   // heights in dB: the note played at the top, this at the line

private:
    void timerCallback() override;
    ParagonX1AudioProcessor& processor;
    std::vector<Echo> shown;
};

// REVERB's tail: silence for PRE-DELAY, then falling to -60 dB over the time its TYPE and SIZE give.
class DecayDisplay final : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::Timer
{
public:
    explicit DecayDisplay (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    float shownSpan() const noexcept { return span; }   // seconds across

private:
    void timerCallback() override;
    ParagonX1AudioProcessor& processor;
    float decay = 0.0f, preDelay = 0.0f, span = 4.0f;
};

// DELAY's or REVERB's page: ON, TYPE and BACK TO RACK along the top, its display, its controls, and the instrument's
// send to it.
class SendPage final : public juce::Component
{
public:
    enum class Kind { delay, reverb };
    SendPage (ParagonX1AudioProcessor&, Kind);
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    Kind getKind() const noexcept { return kind; }
    juce::Component& getDisplay() noexcept { return *display; }
    std::vector<ParamControl*> getControls();
    std::vector<ParamControl*> getSends();

private:
    Kind kind;
    std::unique_ptr<juce::Component> display;
    std::vector<std::unique_ptr<ParamControl>> top, controls, sends;
    juce::TextButton back { "BACK TO RACK" };
    juce::Label sendsLabel;
};

// The SENDS view's strip: how much the instrument sends to DELAY and to REVERB, and DELAY > REVERB.
class SendMatrix final : public juce::Component
{
public:
    explicit SendMatrix (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    ParamControl& send (int which) { return *knobs[(size_t) which]; }

private:
    std::vector<std::unique_ptr<ParamControl>> knobs;
    std::unique_ptr<ParamControl> delayToReverb;
};
