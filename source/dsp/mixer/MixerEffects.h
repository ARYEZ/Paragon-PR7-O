#pragma once

#include "dsp/effects/EffectUtils.h"
#include "dsp/mixer/Mixer.h"

#include <atomic>
#include <memory>
#include <vector>

// THE MIXER's EFFECTS (PR7-D 0.22.0): what can go in a channel's or bus's four slots (inspired by the reference's mixer
// effects; our own set and designs, as the owner chose: "mix core + colour"):
//   EQ          six bands of the plugin's parametric EQ: LOW CUT, a LOW shelf, two bells (MID 1, MID 2), a HIGH shelf,
//               HIGH CUT (zero latency);
//   COMPRESSOR  the plugin's compressor (VCA / OPTO / FET), with a side-chain high-pass (what it hears, not what it
//               passes) and MIX (parallel compression);
//   GATE        ours: it opens above THRESHOLD, stays open for HOLD, closes over RELEASE down by RANGE; its side chain
//               high- and low-passed (to open on the snare, not the hats in its mic);
//   TRANSIENT   ours: ATTACK lifts or softens each hit's front, SUSTAIN its ring, from envelope followers' differences;
//   DRIVE       the plugin's drive pedals (one a side, a DC blocker after, as on the insert buses);
//   LO-FI       the plugin's lo-fi;
//   MOD         the plugin's modulation (chorus, flanger, phaser...);
//   SATURATION  (0.35.0) MARS, PR7-X 0.22.2's multiband saturator (inspired by FabFilter Saturn 2, our own design),
//               unchanged, one engine a side: up to 3 bands, each its STYLE (21: tubes, tapes, transformers, amps,
//               FX), DRIVE (its level kept even automatically), DYNAMICS (gate <-> compress), MIX and LEVEL; the
//               whole slot's MIX and QUALITY (oversampling). Not here: MARS's tone, feedback and modulation.
// Each slot also has ON (a click-free bypass) and OUTPUT.
namespace mixerfx
{
    enum Type { none, eq, compressor, gate, transient, drive, loFi, mod, saturation };
    inline const juce::StringArray typeNames { "None", "EQ", "Compressor", "Gate", "Transient", "Drive", "Lo-Fi", "Mod",
                                               "Saturation" };   // saved: only append
    // SATURATION's values: BANDS, X 1, X 2, MIX, QUALITY, then per band STYLE, DRIVE, DYN, MIX, LEVEL (5 a row).
    inline constexpr int saturationBandsAt = 5, saturationPerBand = 5, saturationBands = 3;
    // How many of a type's settings sit in a row of its editor.
    inline int columnsFor (int type) noexcept { return type == saturation ? 5 : 4; }

    // A type's settings, in the order of FxSlot::values.
    struct Param
    {
        juce::String name;
        float min = 0.0f, max = 1.0f, def = 0.0f;
        juce::String unit;           // "Hz", "dB", "ms", "%", ":1"...
        juce::StringArray choices;   // a choice (the value its index)
        bool log = false;            // a knob that's logarithmic (frequencies, times)
        juce::String offText;        // what the value at `min` (a cut, a filter) or `max` (HIGH CUT) reads as
        bool offAtMax = false;
    };
    const std::vector<Param>& paramsOf (int type);
    mixer::FxSlot defaultSlot (int type);                 // a type's defaults
    juce::String valueText (int type, int index, float value);    // "120 Hz", "OFF", "FET"...

    // One loaded slot's effect (made on the message thread, run on the audio thread).
    class SlotEffect
    {
    public:
        explicit SlotEffect (int t) : type (t) {}
        virtual ~SlotEffect() = default;
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void run (float* left, float* right, int numSamples, const mixer::FxSlot&) noexcept;
        float getReductionDb() const noexcept { return reductionDb.load(); }   // a compressor's or gate's, for its meter

        const int type;

    protected:
        virtual void prepareEffect (double sampleRate, int maxBlockSize) = 0;
        virtual void resetEffect() {}
        virtual void processEffect (float* left, float* right, int numSamples, const std::array<float, mixer::maxFxValues>&) noexcept = 0;
        std::atomic<float> reductionDb { 0.0f };

    private:
        effects::BypassFade bypass;
        juce::SmoothedValue<float> output { 1.0f };
    };
    std::unique_ptr<SlotEffect> make (int type);   // null for none

    // THE EFFECTS of every strip, for the kit (mixer::EffectHost). `sync` (message thread) makes an effect for
    // each loaded slot whose type changed; the audio thread reads them through atomic pointers and the compiled
    // table it's given each block (`setTable`); replaced effects are kept 2 s before they're freed.
    class Host final : public mixer::EffectHost
    {
    public:
        Host();
        void prepare (double sampleRate, int maxBlockSize);   // with the audio stopped
        void sync (const mixer::FxTable&);
        void setTable (const mixer::FxTable* t) noexcept { table = t; }
        void freeRetired();

        bool hasEffects (int strip) const noexcept override;
        void process (int strip, float* left, float* right, int numSamples) noexcept override;
        float getReductionDb (int strip, int slot) const noexcept;
        int getLoadedType (int strip, int slot) const noexcept;   // the type of the effect made for it (tests)

    private:
        static constexpr int count = mixer::numStrips * mixer::numFxSlots;
        std::array<std::atomic<SlotEffect*>, count> live {};
        std::vector<std::shared_ptr<SlotEffect>> owned = std::vector<std::shared_ptr<SlotEffect>> ((size_t) count);
        std::vector<std::pair<std::shared_ptr<SlotEffect>, double>> retired;
        const mixer::FxTable* table = nullptr;
        double sampleRate = 48000.0;
        int blockSize = 512;
    };
}
