#include "Finish.h"

#include "ParagonLookAndFeel.h"

namespace Finish
{
    namespace
    {
        constexpr auto settingKey = "chassisFinish", machineKey = "machineFinish";

        juce::PropertiesFile::Options settingsOptions()
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "Paragon PR7-O";   // its own file, so PR7-O and its siblings each remember their own finish
            options.folderName = "Paragon";
            options.filenameSuffix = "settings";
            options.osxLibrarySubFolder = "Application Support";
            return options;
        }

        struct State
        {
            State()
            {
                // Read once, the first time anything is drawn.
                juce::PropertiesFile file (settingsOptions());
                const auto saved = file.getIntValue (settingKey, 0);
                type = saved == (int) Type::pearlBlack || saved == (int) Type::pearlWhiteNeon ? (Type) saved : Type::pearlWhite;
                const auto machineSaved = file.getIntValue (machineKey, (int) Type::pearlBlack);
                machine = machineSaved >= 0 && machineSaved <= (int) Type::pearlWhiteNeon ? (Type) machineSaved : Type::pearlBlack;
            }

            Type type = Type::pearlWhite, machine = Type::pearlBlack;
            std::optional<Type> painting;   // a component's own finish while it and its controls paint
            juce::ChangeBroadcaster broadcaster;
        };

        State& state()
        {
            // Shared by every PR7-O window open in the DAW (they're in one
            // process), and only touched on the message thread.
            static State instance;
            return instance;
        }
    }

    Type get() { return state().painting.value_or (state().type); }
    void paintAs (std::optional<Type> type) { state().painting = type; }

    void set (Type type, bool remember)
    {
        if (remember)
        {
            juce::PropertiesFile file (settingsOptions());
            file.setValue (settingKey, (int) type);
            file.saveIfNeeded();
        }
        if (state().type != type)
        {
            state().type = type;
            state().broadcaster.sendSynchronousChangeMessage();
        }
    }

    Type getMachine() { return state().machine; }

    void setMachine (Type type, bool remember)
    {
        if (remember)
        {
            juce::PropertiesFile file (settingsOptions());
            file.setValue (machineKey, (int) type);
            file.saveIfNeeded();
        }
        if (state().machine != type)
        {
            state().machine = type;
            state().broadcaster.sendSynchronousChangeMessage();
        }
    }

    juce::ChangeBroadcaster& changes() { return state().broadcaster; }

    juce::Colour ink() { return isBlack() ? juce::Colour (0xfff1eaee) : ParagonColours::ink; }
    juce::Colour inkDim() { return isBlack() ? juce::Colour (0xff9a8f96) : ParagonColours::inkDim; }
    juce::Colour stampEdge() { return isBlack() ? juce::Colours::black.withAlpha (0.75f) : juce::Colours::white.withAlpha (0.7f); }
    juce::Colour printedRed() { return isBlack() ? ParagonColours::glow.brighter (0.15f) : ParagonColours::glow.darker (0.15f); }

    juce::ColourGradient neonGradient (float left, float right)
    {
        // The swatch's colours, 25% darker (at full strength they looked
        // too bright on the unit).
        const auto shade = [] (juce::uint32 argb)
        {
            const juce::Colour colour (argb);
            return juce::Colour::fromFloatRGBA (colour.getFloatRed() * 0.75f, colour.getFloatGreen() * 0.75f, colour.getFloatBlue() * 0.75f, 1.0f);
        };
        juce::ColourGradient gradient (shade (0xffde0639), left, 0.0f, shade (0xffcd6619), right, 0.0f, false);
        gradient.addColour (0.25, shade (0xffd90738));
        gradient.addColour (0.5, shade (0xffd42e2a));
        gradient.addColour (0.75, shade (0xffcf5c1c));
        return gradient;
    }

    juce::Image makeSatinGrain (int width, int height, int seed)
    {
        // A fixed seed keeps it identical every time it's drawn.
        const auto black = isBlack();
        juce::Image grain (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), true);
        juce::Random random (seed);
        juce::Image::BitmapData pixels (grain, juce::Image::BitmapData::writeOnly);
        const auto dark = black ? juce::Colours::black : juce::Colour (0xff5a3a4a);
        const auto speckle = black ? 11.0f : 9.0f;
        for (int y = 0; y < grain.getHeight(); ++y)
        {
            for (int x = 0; x < grain.getWidth(); ++x)
            {
                if (black && random.nextInt (400) == 0)
                {
                    // A fleck: white or rose, like mica in black pearl paint (no
                    // cool tints, which pulled the black towards navy).
                    const juce::Colour flecks[] = { juce::Colours::white, juce::Colour (0xffffc4dc), juce::Colour (0xffffd8c8) };
                    pixels.setPixelColour (x, y, flecks[random.nextInt (3)].withAlpha ((juce::uint8) (30 + random.nextInt (60))));
                    continue;
                }
                if (! black && random.nextInt (500) == 0)
                {
                    // A fleck: white or faintly rose, a little brighter than the speckle.
                    pixels.setPixelColour (x, y, (random.nextBool() ? juce::Colours::white : juce::Colour (0xffffd8e6)).withAlpha ((juce::uint8) (60 + random.nextInt (60))));
                    continue;
                }
                const auto shade = random.nextFloat() * 2.0f - 1.0f;
                const auto alpha = (juce::uint8) juce::roundToInt (std::abs (shade) * speckle);
                pixels.setPixelColour (x, y, (shade > 0.0f ? juce::Colours::white : dark).withAlpha (alpha));
            }
        }
        return grain;
    }
}
