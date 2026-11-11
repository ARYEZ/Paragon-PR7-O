#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

// THE LIBRARIES TO DOWNLOAD (PR7-O 0.13.0; the owner: "do both libraries with GET buttons"; PR7-K 0.71.0's
// SampleLibraries, ported). Free orchestral libraries PR7-O fetches itself, into "Libraries" beside its presets
// folder (never shipped with the plugin): the archive from its publisher, unpacked there, then shown in the library
// browser like a folder the user added. Factory presets name their instruments "{sfz:<folder>}/<path>" (as PR7-K's), so
// they play on any computer: found in the downloaded library, else in a library folder the user added that holds the
// same files.
namespace downloads
{
    struct Library
    {
        juce::String name;          // as the plugin says it
        juce::String url;           // the archive (.tar.gz)
        juce::int64 bytes = 0;      // about its size (progress; GitHub makes these archives, so not exact)
        juce::String folder;        // the folder it unpacks into
        juce::String marker;        // a file inside `folder` that says it's all there
        juce::String browse;        // the folder inside `folder` the browser lists ("": `folder` itself)
        juce::String credit;        // its licence's credit (ABOUT shows it)
        juce::String what;          // in short, on its GET button ("VSCO 2 CE")
        int unpackedMb = 0;         // what it takes on disk once unpacked
    };

    // VSCO 2 Community Edition (Versilian Studios, CC0): its SFZ edition, the GitHub branch "SFZ" pinned to the commit
    // PR7-O was built and checked on (it has no release of its own).
    const Library& vsco();
    // The Sonatina Symphonic Orchestra 4.0 (Mattias Westlund, Peter Eastman; Creative Commons Sampling Plus 1.0): the
    // release PR7-K downloads too.
    const Library& sonatina();
    std::vector<const Library*> all();

    // Where downloads go ("Libraries" beside the presets folder; tests set another), and a library's place there.
    juce::File folder();
    void setFolder (const juce::File&);   // tests
    juce::File rootOf (const Library&);
    bool isInstalled (const Library&);
    // The browser's folder for each installed library.
    juce::Array<juce::File> installedFolders();

    // "{sfz:<folder>}/<path>": how presets name a library's file; the library a reference is in (null: none).
    juce::String refFor (const Library&, const juce::String& path);
    const Library* libraryFor (const juce::String& ref);
    bool isRef (const juce::String&);
    // A reference made a file: in the downloaded library, else the same files in one of `userFolders` (the library
    // folders added in the browser: a copy of VSCO or Sonatina the user has already). Not a reference: as it is.
    // Not found: the file it would be, in the downloads (missing).
    juce::File resolve (const juce::String& refOrPath, const juce::Array<juce::File>& userFolders);
    // A path into a downloaded library made a reference (a preset saved from it plays on another computer).
    juce::String portable (const juce::String& path);

    // Unpacks a .tar.gz into `destination` (regular files and folders; links and anything naming a place outside it
    // are skipped). `progress` 0-1 by the compressed bytes read. False (and why, in `error`) if it isn't a readable
    // tar.gz or `cancel` was set.
    bool extractTarGz (const juce::File& archive, const juce::File& destination, std::function<void (float)> progress,
                       const std::atomic<bool>* cancel, juce::String& error);

    // Fetches and unpacks a library on a thread of its own: `destination` / library.folder once it's done.
    class Download final : private juce::Thread
    {
    public:
        enum class State { idle, downloading, unpacking, done, failed };

        Download();
        ~Download() override;

        // Starts (if not already running). `url` overrides the library's (tests: a file:// URL).
        void start (const Library&, const juce::File& destination, const juce::String& url = {});
        void cancel();

        State getState() const noexcept { return state.load(); }
        const Library& getLibrary() const noexcept { return library; }   // the one it's fetching (or last fetched)
        float getProgress() const noexcept { return progress.load(); }   // of the step it's on, 0-1
        juce::String getMessage() const;                                   // why it failed
        bool isRunning() const noexcept { const auto s = state.load(); return s == State::downloading || s == State::unpacking; }
        bool waitUntilFinished (int timeoutMs);                            // tests
        // Snapshots: as if `library` were at `state`, `progress` of the way (nothing is fetched).
        void showForTest (const Library& l, State s, float p) { library = l; progress = p; state = s; }

    private:
        void run() override;
        void fail (const juce::String&);
        Library library;
        juce::File destination;
        juce::String sourceUrl;
        std::atomic<State> state { State::idle };
        std::atomic<float> progress { 0.0f };
        std::atomic<bool> cancelled { false };
        mutable juce::CriticalSection messageLock;
        juce::String message;
    };
}
