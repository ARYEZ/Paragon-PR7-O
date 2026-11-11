#pragma once

#include <array>

// THE EXPANSIONS (PR7-O 0.13.0; PR7-K's built-in expansions, the owner's ask: "pre-built layouts where it's the entire
// string section or the entire woodwind section or the entire brass section"): themed banks of factory presets after
// the user's banks A-E (bank Rig::numBanks + its index here). Only factory presets live there: a preset the user saves
// is always filed in A-E. Never reorder: a preset's bank is its place in this list. Each plays one of the libraries
// PR7-O can download (library/Downloads.h), named by library reference, so it plays on any computer that has it.
namespace expansions
{
    struct Expansion
    {
        const char* name;          // the bank's name, shown in the browser
        const char* code;          // two letters for the slot name ("VO01")
        const char* description;
    };

    inline constexpr std::array<Expansion, 2> list
    {{
        { "VSCO ORCHESTRA", "VO",
          "Whole sections from VSCO 2 Community Edition by Versilian Studios (CC0): strings, brass and woodwinds, each "
          "instrument on its own stretch of the keyboard, seated on the stage as an orchestra sits." },
        { "SONATINA", "SO",
          "Whole sections from the Sonatina Symphonic Orchestra by Mattias Westlund and Peter Eastman (Creative Commons "
          "Sampling Plus 1.0): five-part strings, brass, the woodwinds with their relatives, and a choir." },
    }};
}
