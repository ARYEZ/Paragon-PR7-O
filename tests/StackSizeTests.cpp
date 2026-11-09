// What gets copied onto the stack stays small. Windows gives a thread 1 MB of stack (Linux 8 MB), and a
// preset and the sampler's settings are copied there for undo, loading and each audio block (in PR7-K:
// 0.15.0's arp patterns, 40 KB inline in ModMatrix, made every test that built the plugin crash on Windows
// while Linux passed). (On Linux, `ulimit -s 1024` before the tests runs them with Windows' stack.)
#include "PluginProcessor.h"
#include "rigs/Rig.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Stack: presets, the sampler's settings and the processor stay small enough to copy on Windows' stack", "[stack]")
{
    CHECK (sizeof (Rig) < 16 * 1024);
    CHECK (sizeof (sampler::Sampler::Settings) < 1024);
    CHECK (sizeof (sampler::Region) < 1024);
    CHECK (sizeof (ParagonX1AudioProcessor) < 128 * 1024);
}
