#include "FactoryRigs.h"

// PR7-O's factory presets. 0.1.0 has only Init: PR7-O ships no samples (the instruments come from the user's own open
// libraries, plan stage 3), so a factory sound would have nothing to play. The library stage brings presets made for
// the libraries the owner uses.
std::vector<Rig> makeFactoryRigs()
{
    Rig init;
    init.name = "Init";
    init.bank = 0;
    init.number = 1;
    init.tags = { "Init" };
    init.notes = "A blank page: no instrument loaded, every setting at its start, dry. Load an SFZ on the INSTRUMENT page.";
    return { init };
}
