// 0.13.0: THE LIBRARIES TO DOWNLOAD. A stand-in archive written here (VSCO 2 CE's folder and file names, a sine), fetched
// from a file:// URL: nothing is downloaded and no library is committed.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestUtils.h"
#include "PluginTestUtils.h"
#include "library/Downloads.h"
#include "library/Library.h"
#include "ui/LibraryBrowser.h"
#include "dsp/sampler/Section.h"

#include <cstring>

namespace
{
    struct TempFolder
    {
        TempFolder() : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7o-dl-" + juce::String (juce::Random::getSystemRandom().nextInt64())))
        {
            dir.createDirectory();
            downloads::setFolder (dir.getChildFile ("Libraries"));
        }
        ~TempFolder() { downloads::setFolder ({}); dir.deleteRecursively(); }
        juce::File dir;
    };

    // A tar archive, written here: ustar headers (GNU long names and POSIX path records when asked).
    struct TarWriter
    {
        juce::MemoryOutputStream data;

        void header (const juce::String& name, juce::int64 size, char type)
        {
            char h[512] {};
            const auto utf8 = name.toRawUTF8();
            std::memcpy (h, utf8, juce::jmin<size_t> (99, std::strlen (utf8)));
            std::snprintf (h + 100, 8, "%07o", 0644);
            std::snprintf (h + 108, 8, "%07o", 0);
            std::snprintf (h + 116, 8, "%07o", 0);
            std::snprintf (h + 124, 12, "%011llo", (unsigned long long) size);
            std::snprintf (h + 136, 12, "%011o", 0);
            h[156] = type;
            std::memcpy (h + 257, "ustar", 5);
            h[263] = '0';
            h[264] = '0';
            std::memset (h + 148, ' ', 8);
            unsigned sum = 0;
            for (auto c : h)
                sum += (std::uint8_t) c;
            std::snprintf (h + 148, 8, "%06o", sum);
            data.write (h, 512);
        }
        void pad (juce::int64 size)
        {
            const char zeros[512] {};
            data.write (zeros, (size_t) ((512 - size % 512) % 512));
        }
        void file (const juce::String& name, const juce::MemoryBlock& content, char how = 0)
        {
            if (how == 'L')
            {
                const auto text = name + juce::String::charToString (0);
                header ("././@LongLink", (juce::int64) text.getNumBytesAsUTF8(), 'L');
                data.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
                pad ((juce::int64) text.getNumBytesAsUTF8());
            }
            else if (how == 'x')
            {
                const auto record = "path=" + name + "\n";
                auto line = juce::String (record.length() + 3) + " " + record;
                line = juce::String (line.length()) + " " + record;
                header ("PaxHeader", line.length(), 'x');
                data.write (line.toRawUTF8(), (size_t) line.length());
                pad (line.length());
            }
            header (how != 0 ? juce::String ("short-name") : name, (juce::int64) content.getSize(), '0');
            data.write (content.getData(), content.getSize());
            pad ((juce::int64) content.getSize());
        }
        void folder (const juce::String& name) { header (name, 0, '5'); }

        // Gzipped, to `file` (`cutAt` > 0: only that many bytes, a broken download).
        void write (const juce::File& file, size_t cutAt = 0)
        {
            const char end[1024] {};
            data.write (end, sizeof (end));
            juce::MemoryOutputStream gz;
            {
                juce::GZIPCompressorOutputStream zip (gz, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
                zip.write (data.getData(), data.getDataSize());
            }
            file.replaceWithData (gz.getData(), cutAt > 0 ? juce::jmin (cutAt, gz.getDataSize()) : gz.getDataSize());
        }
    };

    juce::MemoryBlock text (const juce::String& s) { return { s.toRawUTF8(), s.getNumBytesAsUTF8() }; }

    // A second of a sine at `hz`, mono 16-bit WAV, as a file's bytes.
    juce::MemoryBlock sineWav (double hz)
    {
        juce::MemoryBlock block;
        {
            auto stream = std::make_unique<juce::MemoryOutputStream> (block, false);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out (stream.release());
            auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
            juce::AudioBuffer<float> buffer (1, 48000);
            for (int i = 0; i < 48000; ++i)
                buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 48000.0));
            writer->writeFromAudioSampleBuffer (buffer, 0, 48000);
        }
        return block;
    }

    // The stand-in VSCO: its folder, its marker file (a violin playing a 440 Hz sine), a cello file (220 Hz).
    void writeStandIn (const juce::File& archive, size_t cutAt = 0)
    {
        const auto& library = downloads::vsco();
        TarWriter tar;
        tar.folder (library.folder + "/");
        tar.folder (library.folder + "/Strings/");
        tar.file (library.folder + "/" + library.marker, text ("<region> sample=Strings/a.wav lokey=55 hikey=100 pitch_keycenter=69\n"));
        tar.file (library.folder + "/CelloEns-KS.sfz", text ("<region> sample=Strings/c.wav lokey=36 hikey=60 pitch_keycenter=57\n"));
        tar.file (library.folder + "/Strings/a.wav", sineWav (440.0));
        tar.file (library.folder + "/Strings/c.wav", sineWav (220.0));
        tar.write (archive, cutAt);
    }

    juce::String fileUrl (const juce::File& f) { return juce::URL (f).toString (false); }
}

TEST_CASE ("Downloads: a .tar.gz unpacks, long names too, and nothing lands outside the folder", "[downloads]")
{
    TempFolder t;
    TarWriter tar;
    tar.folder ("lib/");
    tar.file ("lib/a.txt", text ("first"));
    tar.file ("lib/" + juce::String::repeatedString ("long", 40) + "/b.txt", text ("second"), 'L');
    tar.file ("../escaped.txt", text ("never"));
    tar.file ("/abs.txt", text ("never"));
    const auto archive = t.dir.getChildFile ("a.tar.gz");
    tar.write (archive);
    const auto out = t.dir.getChildFile ("out");
    juce::String error;
    REQUIRE (downloads::extractTarGz (archive, out, {}, nullptr, error));
    CHECK (out.getChildFile ("lib/a.txt").loadFileAsString() == "first");
    CHECK (out.getChildFile ("lib/" + juce::String::repeatedString ("long", 40) + "/b.txt").loadFileAsString() == "second");
    CHECK_FALSE (t.dir.getChildFile ("escaped.txt").exists());
}

TEST_CASE ("Downloads: GET fetches and unpacks a library; a cut-off one fails and leaves nothing", "[downloads]")
{
    TempFolder t;
    const auto archive = t.dir.getChildFile ("vsco.tar.gz");
    CHECK_FALSE (downloads::isInstalled (downloads::vsco()));
    {
        writeStandIn (archive, 600);   // (cut off)
        downloads::Download d;
        d.start (downloads::vsco(), downloads::folder(), fileUrl (archive));
        REQUIRE (d.waitUntilFinished (20000));
        CHECK (d.getState() == downloads::Download::State::failed);
        CHECK_FALSE (downloads::isInstalled (downloads::vsco()));
        CHECK_FALSE (downloads::rootOf (downloads::vsco()).exists());
    }
    writeStandIn (archive);
    downloads::Download d;
    d.start (downloads::vsco(), downloads::folder(), fileUrl (archive));
    REQUIRE (d.waitUntilFinished (20000));
    INFO (d.getMessage());
    CHECK (d.getState() == downloads::Download::State::done);
    CHECK (downloads::isInstalled (downloads::vsco()));
    CHECK (downloads::installedFolders().contains (downloads::rootOf (downloads::vsco())));
    CHECK_FALSE (downloads::isInstalled (downloads::sonatina()));
    // Nothing of the archive left behind.
    CHECK (downloads::folder().findChildFiles (juce::File::findFiles, false, "*.download").isEmpty());
}

TEST_CASE ("Downloads: a library reference is found in the download, else in the user's own copy", "[downloads]")
{
    TempFolder t;
    const auto ref = downloads::refFor (downloads::sonatina(), "Sonatina Symphonic Orchestra/Strings - Performance/Violins.sfz");
    CHECK (downloads::isRef (ref));
    CHECK (downloads::libraryFor (ref) == &downloads::sonatina());
    CHECK (downloads::libraryFor ("/some/file.sfz") == nullptr);
    // Not there: the file it would be (missing).
    CHECK_FALSE (downloads::resolve (ref, {}).existsAsFile());
    // The user's own copy, added as the library's inner folder.
    const auto own = t.dir.getChildFile ("My Libraries/Sonatina Symphonic Orchestra");
    own.getChildFile ("Strings - Performance/Violins.sfz").create();
    CHECK (downloads::resolve (ref, { own }) == own.getChildFile ("Strings - Performance/Violins.sfz"));
    // The download, once there, first.
    const auto downloaded = downloads::rootOf (downloads::sonatina()).getChildFile ("Sonatina Symphonic Orchestra/Strings - Performance/Violins.sfz");
    downloaded.create();
    CHECK (downloads::resolve (ref, { own }) == downloaded);
    // A file in a download made a reference; anything else left as it is.
    CHECK (downloads::portable (downloaded.getFullPathName()) == ref);
    // (An absolute path on this system: "/elsewhere/x.sfz" isn't one on Windows, with no drive letter.)
    const auto elsewhere = t.dir.getChildFile ("elsewhere/x.sfz").getFullPathName();
    CHECK (downloads::portable (elsewhere) == elsewhere);
    CHECK (downloads::resolve (elsewhere, {}) == juce::File (elsewhere));
}

TEST_CASE ("Downloads: an instrument or section named by library references plays, and says what's missing", "[downloads][processor]")
{
    TempFolder t;
    const auto archive = t.dir.getChildFile ("vsco.tar.gz");
    writeStandIn (archive);
    {
        downloads::Download d;
        d.start (downloads::vsco(), downloads::folder(), fileUrl (archive));
        REQUIRE (d.waitUntilFinished (20000));
    }
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    const auto play = [&] (int note)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        std::vector<float> out;
        for (int b = 0; b < 60; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            buffer.clear();
            processor.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        processor.processBlock (buffer, off);
        return out;
    };
    // One file by reference: kept as the reference (presets carry it), played from the download.
    const auto violins = downloads::refFor (downloads::vsco(), downloads::vsco().marker);
    processor.loadInstrument (downloads::rootOf (downloads::vsco()).getChildFile (downloads::vsco().marker));
    REQUIRE (processor.waitForInstrument());
    CHECK (processor.getInstrumentFile() == violins);
    CHECK (processor.getLoadProblem().isEmpty());
    CHECK_THAT (frequencyOf (play (69), 2000, 14000), Catch::Matchers::WithinAbs (440.0, 1.0));
    // A section of references: the cellos under the violins.
    sampler::section::Section s;
    s.parts = { { downloads::refFor (downloads::vsco(), "CelloEns-KS.sfz"), 36, 54 }, { violins, 55, 100 } };
    processor.loadSection (s);
    REQUIRE (processor.waitForInstrument());
    REQUIRE (processor.getSection().has_value());
    CHECK (sampler::section::describe (processor.getInstrumentFile()) == "CelloEns-KS + ViolinEns-KS");
    CHECK_THAT (frequencyOf (play (57), 2000, 14000), Catch::Matchers::WithinAbs (220.0, 1.0));
    CHECK_THAT (frequencyOf (play (69), 2000, 14000), Catch::Matchers::WithinAbs (440.0, 1.0));
    // A library not there: no sound, and the screen says which to GET.
    processor.loadInstrument (juce::File());   // (cleared first)
    processor.waitForInstrument();
    processor.setStateInformation (nullptr, 0);
    sampler::section::Section missing;
    missing.parts = { { downloads::refFor (downloads::sonatina(), "Sonatina Symphonic Orchestra/Strings - Performance/Violins.sfz"), 0, 127 } };
    processor.loadSection (missing);
    REQUIRE (processor.waitForInstrument());
    CHECK (processor.getLoadProblem().contains ("needs SONATINA"));
}

TEST_CASE ("Downloads: the browser lists a downloaded library by its name, with GET for the one not there", "[downloads][library]")
{
    TempFolder t;
    const auto archive = t.dir.getChildFile ("vsco.tar.gz");
    writeStandIn (archive);
    {
        downloads::Download d;
        d.start (downloads::vsco(), downloads::folder(), fileUrl (archive));
        REQUIRE (d.waitUntilFinished (20000));
    }
    downloads::Download idle;
    LibraryBrowser browser;
    browser.setSize (1400, 700);
    browser.setDownload (&idle);
    browser.setFolders ({}, false);
    REQUIRE (browser.getEntries().size() == 2);
    CHECK (browser.getEntries()[0].library == "VSCO 2 CE");
    CHECK (LibraryBrowser::displayName (downloads::rootOf (downloads::vsco())) == "VSCO 2 CE");
    CHECK (browser.isDownloadFolder (0));
    auto getVsco = false, getSonatina = false;
    for (auto* c : browser.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->isVisible())
        {
            getVsco = getVsco || b->getButtonText().startsWith ("GET VSCO");
            getSonatina = getSonatina || b->getButtonText().startsWith ("GET SONATINA");
        }
    CHECK_FALSE (getVsco);
    CHECK (getSonatina);
}
