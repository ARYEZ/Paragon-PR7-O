#include "RigLibrary.h"

#include "FactoryRigs.h"

namespace
{
    constexpr auto factoryFavouritesFile = "factory-favourites.txt";

    bool writeRig (const juce::File& file, const Rig& rig)
    {
        // Write to a temporary file and swap it in, so a crash mid-save
        // can't leave a half-written rig.
        juce::TemporaryFile temp (file);
        if (! rig.toXml()->writeTo (temp.getFile()))
            return false;
        return temp.overwriteTargetFileWithTemporary();
    }
}

RigLibrary::RigLibrary (juce::File folderToUse) : folder (std::move (folderToUse))
{
    rescan();
}

juce::File RigLibrary::defaultFolder()
{
    // Windows: %APPDATA%\Paragon\Paragon PR7-O\Rigs
    // macOS:   ~/Library/Application Support/Paragon/Paragon PR7-O/Rigs (via userApplicationDataDirectory)
    // Linux:   ~/.config/Paragon/Paragon PR7-O/Rigs
    // Its own folder, apart from its siblings': PR7-E's mixing rigs don't carry over (they'd mean nothing to a synth).
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
          #if JUCE_MAC
           .getChildFile ("Application Support")
          #endif
           .getChildFile ("Paragon")
           .getChildFile ("Paragon PR7-O")
           .getChildFile ("Rigs");
}

void RigLibrary::rescan()
{
    entries.clear();
    loadFactoryFavourites();

    for (auto& rig : makeFactoryRigs())
    {
        rig.favourite = factoryFavourites.contains (rig.name);
        entries.push_back ({ std::move (rig), {}, true });
    }

    if (folder.isDirectory())
    {
        for (const auto& file : folder.findChildFiles (juce::File::findFiles, false,
                                                         juce::String ("*") + Rig::fileExtension))
        {
            // Be strict about damaged files (e.g. cut short by a crash):
            // JUCE's parser accepts an unclosed opening tag without an error,
            // so the rig would show up blank and could then be saved over.
            // Every rig file we write ends with ">", so require that too.
            const auto text = file.loadFileAsString().trimEnd();
            juce::XmlDocument document (text);
            const auto xml = document.getDocumentElement();
            if (xml == nullptr || document.getLastParseError().isNotEmpty() || ! text.endsWithChar ('>'))
                continue;

            if (auto rig = Rig::fromXml (*xml))
                entries.push_back ({ std::move (*rig), file, false });
        }
    }

    std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
    {
        if (a.rig.bank != b.rig.bank)     return a.rig.bank < b.rig.bank;
        if (a.rig.number != b.rig.number) return a.rig.number < b.rig.number;
        if (a.isFactory != b.isFactory)   return a.isFactory;   // factory first in a shared slot
        return a.rig.name.compareNatural (b.rig.name) < 0;
    });
}

const RigLibrary::Entry* RigLibrary::find (const juce::File& file) const
{
    for (const auto& entry : entries)
        if (! entry.isFactory && entry.file == file)
            return &entry;
    return nullptr;
}

const RigLibrary::Entry* RigLibrary::findFactory (const juce::String& name) const
{
    for (const auto& entry : entries)
        if (entry.isFactory && entry.rig.name == name)
            return &entry;
    return nullptr;
}

juce::String RigLibrary::uniqueName (const juce::String& name) const
{
    const auto base = name.trim().isEmpty() ? juce::String ("New Rig") : name.trim();
    const auto isTaken = [this] (const juce::String& candidate)
    {
        return std::any_of (entries.begin(), entries.end(),
                            [&] (const Entry& e) { return e.rig.name.equalsIgnoreCase (candidate); });
    };

    auto candidate = base;
    for (int suffix = 2; isTaken (candidate); ++suffix)
        candidate = base + " " + juce::String (suffix);
    return candidate;
}

juce::File RigLibrary::uniqueFileFor (const juce::String& name) const
{
    // File names can't hold every character a rig name can (e.g. "/" or ":").
    auto stem = juce::File::createLegalFileName (name).trim();
    if (stem.isEmpty())
        stem = "Rig";
    return folder.getNonexistentChildFile (stem, Rig::fileExtension, false);
}

juce::File RigLibrary::saveAsNew (Rig rig)
{
    if (! folder.createDirectory())
        return {};

    rig.name = uniqueName (rig.name);
    const auto file = uniqueFileFor (rig.name);
    if (! writeRig (file, rig))
        return {};

    rescan();
    return file;
}

bool RigLibrary::overwrite (const juce::File& file, const Rig& rig)
{
    if (! writeRig (file, rig))
        return false;
    rescan();
    return true;
}

bool RigLibrary::remove (const juce::File& file)
{
    // To the bin rather than gone for good, where the OS supports it.
    const auto removed = file.moveToTrash() || file.deleteFile();
    rescan();
    return removed;
}

void RigLibrary::setFavourite (const Entry& entry, bool favourite)
{
    if (entry.isFactory)
    {
        factoryFavourites.removeString (entry.rig.name);
        if (favourite)
            factoryFavourites.add (entry.rig.name);
        saveFactoryFavourites();
        rescan();
    }
    else
    {
        auto rig = entry.rig;
        rig.favourite = favourite;
        overwrite (entry.file, rig);
    }
}

int RigLibrary::nextFreeNumber (int bank) const
{
    for (int number = 1; number <= Rig::maxNumber; ++number)
        if (std::none_of (entries.begin(), entries.end(),
                          [&] (const Entry& e) { return e.rig.bank == bank && e.rig.number == number; }))
            return number;
    return Rig::maxNumber;
}

const RigLibrary::Entry* RigLibrary::step (const Entry* current, int steps) const
{
    if (entries.empty())
        return nullptr;

    const auto count = (int) entries.size();
    int index = -1;
    for (int i = 0; i < count; ++i)
        if (&entries[(size_t) i] == current)
            index = i;

    if (index < 0)
        return &entries[steps > 0 ? 0 : (size_t) (count - 1)];

    return &entries[(size_t) (((index + steps) % count + count) % count)];
}

void RigLibrary::loadFactoryFavourites()
{
    factoryFavourites = juce::StringArray::fromLines (folder.getChildFile (factoryFavouritesFile).loadFileAsString());
    factoryFavourites.removeEmptyStrings();
}

void RigLibrary::saveFactoryFavourites() const
{
    if (folder.createDirectory())
        folder.getChildFile (factoryFavouritesFile).replaceWithText (factoryFavourites.joinIntoString ("\n"));
}
