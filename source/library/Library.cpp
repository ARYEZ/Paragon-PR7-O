#include "library/Library.h"

#include <juce_data_structures/juce_data_structures.h>

namespace library
{
    namespace
    {
        juce::PropertiesFile::Options settingsOptions()
        {
            juce::PropertiesFile::Options options;   // (PR7-O's own settings file, as the finish's: Finish.cpp)
            options.applicationName = "Paragon PR7-O";
            options.folderName = "Paragon";
            options.filenameSuffix = "settings";
            options.osxLibrarySubFolder = "Application Support";
            return options;
        }
        constexpr auto foldersKey = "libraryFolders";

        // The instruments by the words in their names, the more particular first (our own table, from VSCO 2 CE's and
        // Sonatina's names: "ViolinEns-KS", "SViolinVib", "1st Violins Sustain", "Horn Solo Legato", "FHornSus"...).
        struct Word { const char* word; const char* instrument; Section section; };
        const std::vector<Word>& words()
        {
            static const std::vector<Word> list {
                { "1st violin", "1st Violins", Section::strings }, { "2nd violin", "2nd Violins", Section::strings },
                { "violin", "Violins", Section::strings }, { "viola", "Violas", Section::strings },
                { "cello", "Cellos", Section::strings }, { "celli", "Cellos", Section::strings },
                { "contrabass", "Basses", Section::strings }, { " basses ", "Basses", Section::strings },
                { "double bass", "Basses", Section::strings }, { "string bass", "Basses", Section::strings },
                { "all strings", "All Strings", Section::strings },
                { "piccolo", "Piccolo", Section::woodwinds }, { "alto flute", "Alto Flute", Section::woodwinds },
                { "flute", "Flutes", Section::woodwinds }, { "english horn", "English Horn", Section::woodwinds },
                { "englishhorn", "English Horn", Section::woodwinds }, { "cor anglais", "English Horn", Section::woodwinds },
                { "oboe", "Oboes", Section::woodwinds }, { "bass clarinet", "Bass Clarinet", Section::woodwinds },
                { "clarinet", "Clarinets", Section::woodwinds }, { "contrabassoon", "Contrabassoon", Section::woodwinds },
                { "bassoon", "Bassoons", Section::woodwinds }, { "saxophone", "Saxophones", Section::woodwinds },
                { "recorder", "Recorders", Section::woodwinds }, { "all woodwinds", "All Woodwinds", Section::woodwinds },
                { "trumpet", "Trumpets", Section::brass }, { "cornet", "Cornets", Section::brass },
                { "bass trombone", "Bass Trombone", Section::brass }, { "trombone", "Trombones", Section::brass },
                { "tuba", "Tuba", Section::brass }, { "euphonium", "Euphonium", Section::brass },
                { "horn", "Horns", Section::brass }, { "all brass", "All Brass", Section::brass },
                { "timpani", "Timpani", Section::percussion }, { "glock", "Glockenspiel", Section::percussion },
                { "xylo", "Xylophone", Section::percussion }, { "marimba", "Marimba", Section::percussion },
                { "vibraphone", "Vibraphone", Section::percussion }, { "chime", "Chimes", Section::percussion },
                { "tubular", "Chimes", Section::percussion }, { "bell", "Bells", Section::percussion },
                { "snare", "Snare Drum", Section::percussion }, { "bass drum", "Bass Drum", Section::percussion },
                { "bassdrum", "Bass Drum", Section::percussion }, { "cymbal", "Cymbals", Section::percussion },
                { "tam", "Tam-tam", Section::percussion }, { "gong", "Gong", Section::percussion },
                { "triangle", "Triangle", Section::percussion }, { "tambourine", "Tambourine", Section::percussion },
                { "perc", "Percussion", Section::percussion }, { "drum", "Drums", Section::percussion },
                { "harp", "Harp", Section::keys }, { "piano", "Piano", Section::keys }, { "upright", "Piano", Section::keys },
                { "organ", "Organ", Section::keys },
                { "harpsichord", "Harpsichord", Section::keys }, { "celest", "Celesta", Section::keys },
                { "choir", "Choir", Section::choir }, { "chorus", "Choir", Section::choir }, { "chrous", "Choir", Section::choir }, { "soprano", "Sopranos", Section::choir },
                { "alto", "Altos", Section::choir }, { "tenor", "Tenors", Section::choir }, { "voice", "Voices", Section::choir },
            };
            return list;
        }

        // A name spaced out where its words run together ("ViolinEns" -> "violin ens"), lower case.
        juce::String spaced (const juce::String& name)
        {
            juce::String out;
            for (int i = 0; i < name.length(); ++i)
            {
                const auto c = name[i];
                if (i > 0 && juce::CharacterFunctions::isUpperCase (c) && juce::CharacterFunctions::isLowerCase (name[i - 1]))
                    out << ' ';
                out << (c == '-' || c == '_' ? juce::juce_wchar (' ') : c);
            }
            return " " + out.toLowerCase().trim() + " ";
        }

        const Word* wordFor (const juce::String& name)
        {
            const auto n = spaced (name);
            for (const auto& w : words())
            {
                // (bass on its own is a double bass unless it's a bass clarinet / trombone / drum..., caught first)
                if (n.contains (w.word))
                    return &w;
            }
            if (n.contains (" bass ") || n.contains (" basses "))
                return nullptr;   // (handled by the caller: Basses)
            return nullptr;
        }

        bool isSolo (const juce::String& name)
        {
            const auto n = spaced (name);
            if (n.contains (" solo "))
                return true;
            // VSCO 2 CE's solo strings: "SViolin...", "SCello..."
            return name.length() > 1 && name[0] == 'S' && juce::CharacterFunctions::isUpperCase (name[1]);
        }
    }

    juce::String nameOf (Section s)
    {
        static const char* names[] = { "STRINGS", "BRASS", "WOODWINDS", "PERCUSSION", "KEYS & HARP", "CHOIR", "OTHER" };
        return names[(int) s];
    }

    juce::String instrumentOf (const juce::String& name, const juce::String& folder)
    {
        // A name that says nothing of its instrument ("Great - Bourdon 16ft"): its folder's ("Organ").
        if (wordFor (name) == nullptr && ! spaced (name).contains (" bass ") && folder.isNotEmpty())
            if (const auto* w = wordFor (folder))
                return w->instrument;
        const auto* w = wordFor (name.startsWithChar ('S') && isSolo (name) && ! spaced (name).contains (" solo ") ? name.substring (1) : name);
        juce::String instrument = w != nullptr ? juce::String (w->instrument)
                                : (spaced (name).contains (" bass ") ? juce::String ("Basses") : juce::String());
        if (instrument.isEmpty())
            return name;
        if (isSolo (name) && (w == nullptr || w->section != Section::percussion))
        {
            // "Solo Violin": the singular (Basses -> Bass, Horns -> Horn, Violins -> Violin).
            auto single = instrument.startsWith ("1st ") || instrument.startsWith ("2nd ") ? instrument.fromFirstOccurrenceOf (" ", false, false) : instrument;
            if (single.endsWith ("ses"))
                single = single.dropLastCharacters (2);
            else if (single.endsWithChar ('s'))
                single = single.dropLastCharacters (1);
            return "Solo " + single;
        }
        return instrument;
    }

    Section sectionOf (const juce::String& name, const juce::String& folder)
    {
        const auto* w = wordFor (isSolo (name) && ! spaced (name).contains (" solo ") ? name.substring (1) : name);
        if (w != nullptr)
            return w->section;
        if (spaced (name).contains (" bass "))
            return Section::strings;
        // The folder's name ("Strings - Performance", "Percussion"...).
        const auto f = folder.toLowerCase();
        if (f.contains ("string")) return Section::strings;
        if (f.contains ("brass")) return Section::brass;
        if (f.contains ("woodwind") || f.contains ("wind")) return Section::woodwinds;
        if (f.contains ("perc")) return Section::percussion;
        if (f.contains ("key") || f.contains ("piano") || f.contains ("organ")) return Section::keys;
        if (f.contains ("choir") || f.contains ("chorus")) return Section::choir;
        return Section::other;
    }

    std::vector<Entry> scan (const juce::Array<juce::File>& folders)
    {
        std::vector<Entry> entries;
        for (const auto& folder : folders)
        {
            if (! folder.isDirectory())
                continue;
            for (const auto& item : juce::RangedDirectoryIterator (folder, true, "*.sfz", juce::File::findFiles))
            {
                const auto file = item.getFile();
                // (an #include folder's fragments aren't instruments)
                auto fragment = false;
                for (auto parent = file.getParentDirectory(); parent != folder && parent.isDirectory() && ! fragment; parent = parent.getParentDirectory())
                    fragment = parent.getFileName().toLowerCase().startsWith ("include") || parent.getFileName().toLowerCase() == "inc";
                if (fragment)
                    continue;
                Entry e;
                e.file = file;
                e.library = folder.getFileName();
                e.name = file.getFileNameWithoutExtension();
                e.folder = file.getParentDirectory() == folder ? juce::String() : file.getParentDirectory().getRelativePathFrom (folder);
                e.instrument = instrumentOf (e.name, file.getParentDirectory().getFileName());
                e.section = sectionOf (e.name, file.getParentDirectory().getFileName());
                entries.push_back (std::move (e));
            }
        }
        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
        {
            if (a.section != b.section) return a.section < b.section;
            if (a.instrument != b.instrument) return a.instrument.compareNatural (b.instrument) < 0;
            if (a.library != b.library) return a.library.compareNatural (b.library) < 0;
            if (a.name != b.name) return a.name.compareNatural (b.name) < 0;
            return a.folder.compareNatural (b.folder) < 0;
        });
        return entries;
    }

    juce::Array<juce::File> folders()
    {
        juce::PropertiesFile file (settingsOptions());
        juce::Array<juce::File> result;
        for (const auto& path : juce::StringArray::fromLines (file.getValue (foldersKey)))
            if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
                result.addIfNotAlreadyThere (juce::File (path));
        return result;
    }

    void setFolders (const juce::Array<juce::File>& list)
    {
        juce::StringArray paths;
        for (const auto& f : list)
            paths.addIfNotAlreadyThere (f.getFullPathName());
        juce::PropertiesFile file (settingsOptions());
        file.setValue (foldersKey, paths.joinIntoString ("\n"));
        file.saveIfNeeded();
    }
}
