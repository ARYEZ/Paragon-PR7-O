cmake_minimum_required(VERSION 3.24)

# macOS: build one universal binary for Apple Silicon and Intel Macs.
# These must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(ParagonX1 VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PARAGON_BUILD_TESTS "Build the unit tests" ON)

include(cmake/Dependencies.cmake)

juce_add_plugin(ParagonX1
    PRODUCT_NAME             "Paragon PR7-O" # the orchestra instrument of the PR7 family: its own name, bundle ID and plugin code, so it installs beside the others
    COMPANY_NAME             "Paragon"
    BUNDLE_ID                "com.paragon.paragonpr7o"
    PLUGIN_MANUFACTURER_CODE Prgn     # four characters, at least one upper case
    PLUGIN_CODE              Pr7o     # four characters, exactly one upper case; never PR7-G's Px1a, PR7-B's Pr7b, PR7-V's Pr7v, PR7-C's Pr7c, PR7-E's Pr7e, PR7-K's Pr7k or PR7-D's Pr7d
    FORMATS                  VST3
    IS_SYNTH                 TRUE     # an instrument: MIDI in, sound out
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    VST3_CATEGORIES          Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD  FALSE)

# Inter (SIL Open Font License, see resources/fonts/OFL-LICENSE.txt), built
# into the plugin so it looks the same on every computer, as in the mockup.
juce_add_binary_data(ParagonX1Fonts
    HEADER_NAME ParagonFonts.h
    NAMESPACE   ParagonFonts
    SOURCES
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/InterDisplay-BlackItalic.ttf)

# 0.5.0: THE ORCHESTRA VIEW's pictures, our own 3D string family rendered in Blender (resources/orchestra/README.md).
juce_add_binary_data(ParagonX1Orchestra
    HEADER_NAME OrchestraImages.h
    NAMESPACE   OrchestraImages
    SOURCES
        resources/orchestra/violin.jpg
        resources/orchestra/viola.jpg
        resources/orchestra/cello.jpg
        resources/orchestra/bass.jpg
        resources/orchestra/section.jpg
        resources/orchestra/section-lit.jpg
        resources/orchestra/trumpet.jpg
        resources/orchestra/horn.jpg
        resources/orchestra/trombone.jpg
        resources/orchestra/tuba.jpg
        resources/orchestra/brass-section.jpg
        resources/orchestra/brass-section-lit.jpg
        resources/orchestra/stage.jpg
        resources/orchestra/orchestra.json)

target_sources(ParagonX1
    PRIVATE
        source/Parameters.cpp
        source/RackParameters.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        # THE INSTRUMENT (PR7-O 0.1.0): SFZ instruments played by our sampler.
        source/dsp/sampler/Sample.cpp
        source/dsp/sampler/Sfz.cpp
        source/dsp/sampler/Section.cpp
        source/dsp/stage/Seating.cpp
        source/library/Library.cpp
        source/dsp/stage/Stage.cpp
        source/dsp/sampler/Sampler.cpp
        source/dsp/sampler/DiskStreamer.cpp    # PR7-D's disk streaming, kept for the libraries (plan stage 3)
        source/dsp/mixer/MixerEffects.cpp      # PR7-D's mixer effects, kept for the stage (plan stage 6)
        # The effects: PR7-E's rack (ECHELON, DOMINION, CONSTRICTOR, the LIMITER) and PR7-G's sends and inserts.
        source/dsp/dominion/Dominion.cpp
        source/dsp/effects/Compressor.cpp
        source/dsp/effects/MultibandComp.cpp
        source/dsp/effects/Mars.cpp
        source/dsp/effects/MarsModulation.cpp
        source/dsp/effects/Limiter.cpp
        source/dsp/effects/MasterLimiter.cpp
        source/dsp/effects/Loudness.cpp
        source/dsp/effects/Delay.cpp
        source/dsp/effects/Reverb.cpp
        source/dsp/effects/Aeon.cpp
        source/dsp/effects/Modulation.cpp
        source/dsp/effects/LoFi.cpp
        source/dsp/effects/Drive.cpp
        source/dsp/effects/Glitch.cpp
        source/dsp/effects/Abyss.cpp
        source/dsp/effects/Chimera.cpp
        source/dsp/effects/Cosmonaut.cpp
        source/dsp/effects/Halo.cpp
        source/dsp/effects/Rabid.cpp
        source/dsp/effects/PitchShifter.cpp
        source/dsp/eq/EqDesign.cpp
        source/dsp/eq/ParametricEq.cpp
        source/dsp/eq/EqMatch.cpp
        source/dsp/eq/EqMatchSession.cpp
        source/rigs/Rig.cpp
        source/rigs/FactoryRigs.cpp
        source/rigs/CompPresets.cpp
        source/rigs/DominionPresets.cpp
        source/rigs/RigLibrary.cpp
        source/ui/LevelMeter.cpp
        source/ui/Logo.cpp
        source/ui/RigBrowser.cpp
        source/ui/Icons.cpp
        source/ui/DockPanels.cpp
        source/ui/SynthControls.cpp
        source/ui/PedalBody.cpp
        source/ui/InstrumentRack.cpp
        source/ui/OrchestraView.cpp
        source/ui/StageView.cpp
        source/ui/LibraryBrowser.cpp
        source/ui/SynthDisplays.cpp
        source/ui/InstrumentPage.cpp
        source/ui/ProgressBox.cpp
        source/ui/FxPage.cpp
        source/ui/SendPages.cpp
        source/ui/ModFxPages.cpp
        source/ui/InsertPages.cpp
        source/ui/EqPage.cpp
        source/ui/EqDisplay.cpp
        source/ui/EqMatchPanel.cpp
        source/ui/CompPage.cpp
        source/ui/DominionPage.cpp
        source/ui/LimiterPage.cpp
        source/ui/PresetButton.cpp
        source/ui/RackUnit.cpp
        source/ui/RackSpecs.cpp
        source/ui/Footswitch.cpp
        source/ui/LedDisplay.cpp
        source/ui/MainView.cpp
        source/ui/ParagonLookAndFeel.cpp
        source/ui/Finish.cpp)

target_include_directories(ParagonX1 PUBLIC source)

target_compile_definitions(ParagonX1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=0)

target_link_libraries(ParagonX1
    PRIVATE
        ParagonX1Fonts
        ParagonX1Orchestra
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_warning_flags   # strict warnings for our code only
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(PARAGON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
