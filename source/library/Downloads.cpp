#include "library/Downloads.h"

#include "rigs/RigLibrary.h"

#include <algorithm>
#include <cstring>

namespace downloads
{
    const Library& vsco()
    {
        static const Library library {
            "VSCO 2 Community Edition",
            "https://github.com/sgossner/VSCO-2-CE/archive/6dd651d55dde97fd4028699be9d4481f26917891.tar.gz",
            2400000000,                 // (about: its 16-bit WAVs, 2.5 GB unpacked, hardly compress)
            "VSCO-2-CE-6dd651d55dde97fd4028699be9d4481f26917891",
            "ViolinEns-KS.sfz",
            "",
            "VSCO 2 Community Edition by Versilian Studios (CC0), github.com/sgossner/VSCO-2-CE",
            "VSCO 2 CE",
            2600
        };
        return library;
    }

    const Library& sonatina()
    {
        static const Library library {
            "Sonatina Symphonic Orchestra",
            "https://github.com/peastman/sso/archive/refs/tags/v4.0.tar.gz",
            1377280000,                 // (PR7-K's measure of the same tag's archive: GitHub's is about this)
            "sso-4.0",
            "Sonatina Symphonic Orchestra/Brass - Notation/Horns Sustain.sfz",
            "Sonatina Symphonic Orchestra",
            "Sonatina Symphonic Orchestra by Mattias Westlund and Peter Eastman (Creative Commons Sampling Plus 1.0), "
            "github.com/peastman/sso",
            "SONATINA",
            1420
        };
        return library;
    }

    std::vector<const Library*> all() { return { &vsco(), &sonatina() }; }

    namespace
    {
        juce::File& folderOverride()
        {
            static juce::File f;
            return f;
        }
    }

    juce::File folder()
    {
        if (folderOverride() != juce::File())
            return folderOverride();
        return RigLibrary::defaultFolder().getParentDirectory().getChildFile ("Libraries");
    }

    void setFolder (const juce::File& f) { folderOverride() = f; }
    juce::File rootOf (const Library& l) { return folder().getChildFile (l.folder); }
    bool isInstalled (const Library& l) { return rootOf (l).getChildFile (l.marker).existsAsFile(); }

    juce::Array<juce::File> installedFolders()
    {
        juce::Array<juce::File> list;
        for (const auto* l : all())
            if (isInstalled (*l))
                list.add (l->browse.isEmpty() ? rootOf (*l) : rootOf (*l).getChildFile (l->browse));
        return list;
    }

    juce::String refFor (const Library& l, const juce::String& path) { return "{sfz:" + l.folder + "}/" + path; }
    bool isRef (const juce::String& s) { return s.startsWith ("{sfz:") && s.contains ("}/"); }

    const Library* libraryFor (const juce::String& ref)
    {
        for (const auto* l : all())
            if (ref.startsWith ("{sfz:" + l->folder + "}/"))
                return l;
        return nullptr;
    }

    juce::File resolve (const juce::String& refOrPath, const juce::Array<juce::File>& userFolders)
    {
        if (! isRef (refOrPath))
            return juce::File::isAbsolutePath (refOrPath) ? juce::File (refOrPath) : juce::File();
        const auto name = refOrPath.fromFirstOccurrenceOf ("{sfz:", false, false).upToFirstOccurrenceOf ("}/", false, false);
        const auto path = refOrPath.fromFirstOccurrenceOf ("}/", false, false);
        const auto downloaded = folder().getChildFile (name).getChildFile (path);
        if (downloaded.existsAsFile())
            return downloaded;
        // The same files in a folder the user added: the path, or its tail (the user's folder may be the library's
        // own top, or a folder inside it: "Sonatina Symphonic Orchestra").
        const auto parts = juce::StringArray::fromTokens (path, "/", "");
        for (const auto& f : userFolders)
            for (int skip = 0; skip < parts.size(); ++skip)
            {
                juce::StringArray tail;
                for (int i = skip; i < parts.size(); ++i)
                    tail.add (parts[i]);
                if (const auto candidate = f.getChildFile (tail.joinIntoString ("/")); candidate.existsAsFile())
                    return candidate;
            }
        return downloaded;
    }

    juce::String portable (const juce::String& path)
    {
        if (isRef (path) || ! juce::File::isAbsolutePath (path))
            return path;
        const juce::File file (path);
        for (const auto* l : all())
            if (file.isAChildOf (rootOf (*l)))
                return refFor (*l, file.getRelativePathFrom (rootOf (*l)).replaceCharacter ('\\', '/'));
        return path;
    }

    namespace
    {
        // A tar header's number: octal text (GNU's base-256 form for big files too).
        juce::int64 tarNumber (const char* field, int length)
        {
            if ((std::uint8_t) field[0] & 0x80)
            {
                juce::int64 value = 0;
                for (int i = 1; i < length; ++i)
                    value = (value << 8) | (std::uint8_t) field[i];
                return value;
            }
            juce::int64 value = 0;
            for (int i = 0; i < length && field[i] != 0; ++i)
                if (field[i] >= '0' && field[i] <= '7')
                    value = value * 8 + (field[i] - '0');
            return value;
        }

        juce::String tarText (const char* field, int length)
        {
            int n = 0;
            while (n < length && field[n] != 0)
                ++n;
            return juce::String::fromUTF8 (field, n);
        }

        // A path from the archive, inside `destination`, or none if it would lead anywhere else.
        juce::File safeChild (const juce::File& destination, juce::String path)
        {
            path = path.replaceCharacter ('\\', '/');
            while (path.startsWith ("./"))
                path = path.substring (2);
            if (path.isEmpty() || path.startsWithChar ('/') || path.containsChar (':'))
                return {};
            for (const auto& part : juce::StringArray::fromTokens (path, "/", {}))
                if (part == "..")
                    return {};
            return destination.getChildFile (path);
        }
    }

    bool extractTarGz (const juce::File& archive, const juce::File& destination, std::function<void (float)> progress,
                       const std::atomic<bool>* cancel, juce::String& error)
    {
        auto file = std::make_unique<juce::FileInputStream> (archive);
        if (! file->openedOk())
        {
            error = "Couldn't open " + archive.getFileName();
            return false;
        }
        const auto total = juce::jmax<juce::int64> (1, file->getTotalLength());
        auto* raw = file.get();
        juce::GZIPDecompressorInputStream tar (file.release(), true, juce::GZIPDecompressorInputStream::gzipFormat);
        if (! destination.createDirectory())
        {
            error = "Couldn't make " + destination.getFullPathName();
            return false;
        }
        std::vector<char> buffer (1 << 16);
        juce::String longName;
        auto files = 0;
        for (;;)
        {
            if (cancel != nullptr && cancel->load())
            {
                error = "Cancelled";
                return false;
            }
            char header[512];
            if (tar.read (header, 512) != 512)
                break;
            if (std::all_of (header, header + 512, [] (char c) { return c == 0; }))
                break;   // the end (two empty blocks)
            if (std::memcmp (header + 257, "ustar", 5) != 0 && tarNumber (header + 148, 8) == 0)
            {
                error = archive.getFileName() + " isn't a tar archive";
                return false;
            }
            const auto size = tarNumber (header + 124, 12);
            const auto type = header[156];
            auto name = tarText (header, 100);
            if (std::memcmp (header + 257, "ustar", 5) == 0)
                if (const auto prefix = tarText (header + 345, 155); prefix.isNotEmpty())
                    name = prefix + "/" + name;
            if (longName.isNotEmpty())
            {
                name = longName;
                longName = {};
            }
            const auto padded = (size + 511) / 512 * 512;

            if (type == 'L' || type == 'x' || type == 'g')
            {
                // A long name (GNU) or extended header (POSIX: "length path=...\n"): for the next entry.
                juce::MemoryBlock data;
                if (size > (1 << 20) || (juce::int64) tar.readIntoMemoryBlock (data, (juce::pointer_sized_int) padded) != padded)
                {
                    error = "A damaged entry in " + archive.getFileName();
                    return false;
                }
                const auto* bytes = (const char*) data.getData();
                auto length = (int) size;
                while (length > 0 && bytes[length - 1] == 0)   // (a long name ends with a NUL)
                    --length;
                const auto text = juce::String::fromUTF8 (bytes, length);
                if (type == 'L')
                    longName = text;
                else if (type == 'x')
                    for (const auto& line : juce::StringArray::fromLines (text))
                        if (line.contains (" path="))
                            longName = line.fromFirstOccurrenceOf (" path=", false, false);
                continue;
            }

            const auto target = safeChild (destination, name);
            if (type == '5')
            {
                if (target != juce::File())
                    target.createDirectory();
            }
            else if (type == '0' || type == 0)
            {
                std::unique_ptr<juce::FileOutputStream> out;
                if (target != juce::File())
                {
                    target.getParentDirectory().createDirectory();
                    target.deleteFile();
                    out = target.createOutputStream();
                    if (out == nullptr || ! out->openedOk())
                    {
                        error = "Couldn't write " + target.getFullPathName();
                        return false;
                    }
                }
                for (juce::int64 left = size; left > 0;)
                {
                    const auto n = (int) juce::jmin<juce::int64> (left, (juce::int64) buffer.size());
                    if (tar.read (buffer.data(), n) != n)
                    {
                        error = archive.getFileName() + " ends early";
                        return false;
                    }
                    if (out != nullptr && ! out->write (buffer.data(), (size_t) n))
                    {
                        error = "Couldn't write " + target.getFullPathName() + " (is the disk full?)";
                        return false;
                    }
                    left -= n;
                }
                tar.skipNextBytes (padded - size);
                ++files;
            }
            else
                tar.skipNextBytes (padded);   // links and the like: left out
            if (progress)
                progress ((float) juce::jlimit (0.0, 1.0, (double) raw->getPosition() / (double) total));
        }
        if (files == 0)
        {
            error = archive.getFileName() + " holds no files";
            return false;
        }
        return true;
    }

    //==========================================================================
    Download::Download() : juce::Thread ("PR7-O library download") {}

    Download::~Download()
    {
        cancelled = true;
        stopThread (10000);
    }

    void Download::start (const Library& toFetch, const juce::File& folder, const juce::String& url)
    {
        if (isRunning() || isThreadRunning())
            return;
        library = toFetch;
        destination = folder;
        sourceUrl = url.isNotEmpty() ? url : toFetch.url;
        cancelled = false;
        progress = 0.0f;
        {
            const juce::ScopedLock lock (messageLock);
            message = {};
        }
        state = State::downloading;
        startThread (juce::Thread::Priority::low);
    }

    void Download::cancel()
    {
        cancelled = true;
    }

    juce::String Download::getMessage() const
    {
        const juce::ScopedLock lock (messageLock);
        return message;
    }

    bool Download::waitUntilFinished (int timeoutMs)
    {
        return waitForThreadToExit (timeoutMs) && ! isRunning();
    }

    void Download::fail (const juce::String& why)
    {
        {
            const juce::ScopedLock lock (messageLock);
            message = cancelled.load() ? juce::String ("Cancelled") : why;
        }
        state = State::failed;
    }

    void Download::run()
    {
        destination.createDirectory();
        const auto archive = destination.getChildFile (library.folder + ".tar.gz.download");
        archive.deleteFile();

        // 1. The archive, read in pieces (so it can be cancelled and its progress shown).
        {
            int status = 0;
            std::unique_ptr<juce::InputStream> in (juce::URL (sourceUrl).createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (20000)
                    .withStatusCode (&status)
                    .withNumRedirectsToFollow (5)));
            if (in == nullptr || (status != 0 && status != 200))
                return fail ("Couldn't reach " + juce::URL (sourceUrl).getDomain() + (status != 0 ? " (" + juce::String (status) + ")" : juce::String())
                             + ". Check the internet connection and try again.");
            auto out = archive.createOutputStream();
            if (out == nullptr)
                return fail ("Couldn't write to " + destination.getFullPathName());
            const auto expected = in->getTotalLength() > 0 ? in->getTotalLength() : library.bytes;
            std::vector<char> buffer (1 << 16);
            juce::int64 got = 0;
            for (;;)
            {
                if (cancelled.load() || threadShouldExit())
                {
                    out.reset();
                    archive.deleteFile();
                    return fail ("Cancelled");
                }
                const auto n = in->read (buffer.data(), (int) buffer.size());
                if (n <= 0)
                    break;
                if (! out->write (buffer.data(), (size_t) n))
                {
                    out.reset();
                    archive.deleteFile();
                    return fail ("Couldn't save the download (is the disk full? It needs about " + juce::String (2 * library.bytes / 1000000000.0 + 0.1, 1)
                             + " GB free while it unpacks).");
                }
                got += n;
                progress = (float) juce::jlimit (0.0, 1.0, (double) got / (double) juce::jmax<juce::int64> (1, expected));
            }
            out.reset();
            // (GitHub's archives have no fixed size: one cut off fails to unpack, below)
        }

        // 2. Unpacked (a folder left from an earlier try is cleared first), then the archive goes.
        state = State::unpacking;
        progress = 0.0f;
        destination.getChildFile (library.folder).deleteRecursively();
        juce::String error;
        const auto ok = extractTarGz (archive, destination, [this] (float p) { progress = p; }, &cancelled, error);
        archive.deleteFile();
        if (! ok)
        {
            destination.getChildFile (library.folder).deleteRecursively();
            return fail ("Couldn't unpack it: " + error);
        }
        if (! destination.getChildFile (library.folder).getChildFile (library.marker).existsAsFile())
            return fail ("The download doesn't hold " + library.marker);
        progress = 1.0f;
        state = State::done;
    }
}
