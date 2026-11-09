#pragma once

#include "PluginProcessor.h"
#include "ui/CompPage.h"
#include "ui/DominionPage.h"
#include "ui/EqPage.h"
#include "ui/InsertPages.h"
#include "ui/LimiterPage.h"
#include "ui/ModFxPages.h"
#include "ui/RackUnit.h"
#include "ui/SendPages.h"

// The FX page: four views, INSERTS, SENDS, MOD / LO-FI and MASTER (tabs at the top, in the sound's order).
//   INSERTS (stage 7d): the insert buses A to D (one shown at a time: its strip with the instrument routed to it and
//   its sends, then its DRIVE, GLITCH, MOD and LO-FI units).
//   SENDS (stage 7b): PR7-G's DELAY and REVERB as two rack units, and the send strip (the instrument's sends,
//   DELAY > REVERB). Their returns join the kit before the inserts.
//   MOD / LO-FI (stage 7c): PR7-G's MOD block twice and its LO-FI, on the whole sound (the returns too), before MASTER.
//   MASTER (stage 7a): THE RACK, PR7-E 0.18.1's four rack units in the signal chain's order (ECHELON, DOMINION,
//   CONSTRICTOR, the LIMITER), each with its POWER switch, meter, screen, keys and knobs.
// A unit's EDIT (or its name) opens that effect's big page here; the page's BACK TO RACK returns to its view.
class FxPage final : public juce::Component
{
public:
    explicit FxPage (ParagonX1AudioProcessor&);

    enum class View { rack, eq, dominion, comp, limiter,   // rack: the MASTER view
                      sends, delay, reverb,                // stage 7b
                      modFx, mod, mod2, loFi,              // stage 7c
                      inserts, insertDrive, insertGlitch, insertMod, insertLoFi };   // stage 7d (the strip's bus)
    void show (View);
    View getView() const noexcept { return view; }

    RackUnit& getUnit (int index) noexcept { return *units[(size_t) index]; }
    RackUnit& getSendUnit (int index) noexcept { return *sendUnits[(size_t) index]; }
    SendPage& getDelayPage() noexcept { return delayPage; }
    SendPage& getReverbPage() noexcept { return reverbPage; }
    SendMatrix& getSendMatrix() noexcept { return matrix; }
    RackUnit& getModFxUnit (int index) noexcept { return *modFxUnits[(size_t) index]; }   // MOD, MOD 2, LO-FI
    ModFxPage& getModFxPage (int index) noexcept { return *modFxPages[(size_t) index]; }
    juce::TextButton& getModFxTab() noexcept { return modFxTab; }
    juce::TextButton& getInsertsTab() noexcept { return insertsTab; }
    InsertBusStrip& getInsertStrip() noexcept { return insertStrip; }
    RackUnit& getInsertUnit (int bus, int index) noexcept { return *insertUnits[(size_t) bus][(size_t) index]; }
    InsertEffectPage& getInsertEffectPage (int bus, int index) noexcept { return *insertEffectPages[(size_t) bus][(size_t) index]; }   // DRIVE, GLITCH
    ModFxPage& getInsertModFxPage (int bus, int index) noexcept { return *insertModFxPages[(size_t) bus][(size_t) index]; }       // MOD, LO-FI
    juce::TextButton& getSendsTab() noexcept { return sendsTab; }
    juce::TextButton& getMasterTab() noexcept { return masterTab; }
    EqPage& getEqPage() noexcept { return eqPage; }
    DominionPage& getDominionPage() noexcept { return dominionPage; }
    CompPage& getCompPage() noexcept { return compPage; }
    LimiterPage& getLimiterPage() noexcept { return limiterPage; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    View view = View::rack;
    std::array<std::unique_ptr<RackUnit>, 4> units;
    std::array<std::unique_ptr<RackUnit>, 2> sendUnits;
    std::array<std::unique_ptr<RackUnit>, 3> modFxUnits;
    std::array<std::unique_ptr<ModFxPage>, 3> modFxPages;
    juce::TextButton insertsTab { "INSERTS" }, sendsTab { "SENDS" }, modFxTab { "MOD / LO-FI" }, masterTab { "MASTER" };
    InsertBusStrip insertStrip;
    std::array<std::array<std::unique_ptr<RackUnit>, 4>, ParamIDs::numInsertBuses> insertUnits;
    std::array<std::array<std::unique_ptr<InsertEffectPage>, 2>, ParamIDs::numInsertBuses> insertEffectPages;
    std::array<std::array<std::unique_ptr<ModFxPage>, 2>, ParamIDs::numInsertBuses> insertModFxPages;
    SendMatrix matrix;
    SendPage delayPage, reverbPage;
    EqPage eqPage;
    DominionPage dominionPage;
    CompPage compPage;
    LimiterPage limiterPage;
};
