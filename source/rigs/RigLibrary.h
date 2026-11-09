#pragma once

#include "Rig.h"

#include <vector>

// All the rigs the user can choose from: the built-in factory rigs plus the
// user's own, which are files in a folder (one .pr7rig file per rig) so they
// can be backed up, shared, and used from any DAW project.
//
// Message thread only.
class RigLibrary
{
public:
    struct Entry
    {
        Rig rig;
        juce::File file;        // empty for factory rigs
        bool isFactory = false;
    };

    // `folder` defaults to the user's Paragon PR7-O presets folder; tests pass a temporary one.
    explicit RigLibrary (juce::File folder = defaultFolder());

    static juce::File defaultFolder();

    const juce::File& getFolder() const noexcept { return folder; }

    // Re-reads the folder. Files that aren't valid rigs are skipped.
    void rescan();

    // Sorted by bank, then number, then name: the order the < > buttons step through.
    const std::vector<Entry>& getEntries() const noexcept { return entries; }
    const Entry* find (const juce::File& file) const;
    const Entry* findFactory (const juce::String& name) const;

    // Writes a new user rig. If `rig.name` is taken, adds " 2", " 3"...
    // Returns the new rig's file, or an empty File if it couldn't be written.
    juce::File saveAsNew (Rig rig);

    // Overwrites an existing user rig's file.
    bool overwrite (const juce::File& file, const Rig& rig);

    bool remove (const juce::File& file);

    // Works for factory rigs too (remembered in a small file in the folder).
    void setFavourite (const Entry& entry, bool favourite);

    // The lowest number not used by any rig in `bank`.
    int nextFreeNumber (int bank) const;

    // The entry `steps` away from `current` in sorted order, wrapping round.
    // With no current rig, +1 gives the first rig and -1 the last.
    const Entry* step (const Entry* current, int steps) const;

private:
    juce::File uniqueFileFor (const juce::String& name) const;
    juce::String uniqueName (const juce::String& name) const;
    void loadFactoryFavourites();
    void saveFactoryFavourites() const;

    juce::File folder;
    std::vector<Entry> entries;
    juce::StringArray factoryFavourites;
};
