#include "dsp/stage/Seating.h"

namespace stage
{
    Seat clamp (Seat s) noexcept
    {
        s.x = juce::jlimit (-halfWidth, halfWidth, s.x);
        s.y = juce::jlimit (front, back, s.y);
        s.width = juce::jlimit (0.2f, 2.0f * halfWidth, s.width);
        return s;
    }

    const std::vector<Place>& places()
    {
        static const std::vector<Place> list {
            { "VIOLINS",        { -4.6f, 2.6f, 3.6f } },
            { "VIOLINS II",     { -1.9f, 4.0f, 2.6f } },
            { "VIOLAS",         {  1.0f, 4.0f, 2.8f } },
            { "CELLOS",         {  3.9f, 2.6f, 3.2f } },
            { "BASSES",         {  6.9f, 5.4f, 1.8f } },
            { "FLUTES",         { -1.3f, 7.0f, 1.6f } },
            { "OBOES",          {  1.3f, 7.0f, 1.6f } },
            { "CLARINETS",      { -1.3f, 8.0f, 1.6f } },
            { "BASSOONS",       {  1.3f, 8.0f, 1.6f } },
            { "HORNS",          { -4.9f, 8.3f, 2.6f } },
            { "TRUMPETS",       {  0.0f, 9.5f, 2.0f } },
            { "TROMBONES",      {  3.8f, 9.4f, 2.2f } },
            { "TUBA",           {  6.0f, 9.3f, 0.8f } },
            { "TIMPANI",        { -1.8f, 10.5f, 1.6f } },
            { "PERCUSSION",     { -5.6f, 10.4f, 2.6f } },
            { "HARP",           { -7.6f, 5.2f, 0.8f } },
            { "KEYS",           { -7.2f, 7.6f, 1.2f } },
        };
        return list;
    }

    Seat seatFor (const juce::String& name)
    {
        const auto n = name.toLowerCase();
        const auto place = [] (const char* which)
        {
            for (const auto& p : places())
                if (juce::String (p.name) == which)
                    return p.seat;
            return Seat {};
        };
        const auto has = [&n] (std::initializer_list<const char*> words)
        {
            for (const auto* w : words)
                if (n.contains (w))
                    return true;
            return false;
        };
        if (has ({ "viola", "vla" }))                                       return place ("VIOLAS");
        if (has ({ "violin", "vln" }))
        {
            if (has ({ "sviolin", "solo" }))                                 return { -1.6f, 1.6f, 0.5f };   // the leader's chair
            if (has ({ "violin 2", "violins 2", "violin2", "violinii", "violin ii", "second" })) return place ("VIOLINS II");
            return place ("VIOLINS");
        }
        if (has ({ "cello", "vcl" }))                                        return place ("CELLOS");
        if (has ({ "trombone", "tbn" }))                                     return place ("TROMBONES");
        if (has ({ "tuba" }))                                                return place ("TUBA");
        if (has ({ "trumpet", "tpt", "cornet" }))                            return place ("TRUMPETS");
        if (has ({ "english horn", "englishhorn", "cor anglais", "oboe" }))  return place ("OBOES");
        if (has ({ "horn" }))                                                return place ("HORNS");
        if (has ({ "contrabassoon", "contra bassoon", "contrafagott" }))     return place ("BASSOONS");   // (0.12.0: not a double bass)
        if (has ({ "contrabass", "double bass", "doublebass", "string bass" }) || (n.contains ("bass") && ! has ({ "bassoon", "clarinet", "drum", "flute" })))
            return place ("BASSES");
        if (has ({ "flute", "piccolo", "fl " }))                             return place ("FLUTES");
        if (has ({ "clarinet" }))                                            return place ("CLARINETS");
        if (has ({ "bassoon", "fagott" }))                                   return place ("BASSOONS");
        if (has ({ "timpani", "timp" }))                                     return place ("TIMPANI");
        if (has ({ "harp" }))                                                return place ("HARP");
        if (has ({ "piano", "celest", "organ", "harpsichord" }))             return place ("KEYS");
        if (has ({ "drum", "cymbal", "perc", "glock", "xylo", "marimba", "vibra", "bell", "chime", "gong", "tam", "triangle", "snare", "ratchet" }))
            return place ("PERCUSSION");
        return { 0.0f, 5.5f, 3.0f };   // anything else: the middle
    }
}
