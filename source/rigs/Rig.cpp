#include "Rig.h"

#include "Parameters.h"
#include "rigs/Expansions.h"

namespace
{
    // Bump when the file layout changes in a way older code can't read.
    constexpr int fileVersion = 1;

    // Which plugin version's parameters the preset was saved with, for the occasional parameter whose default
    // changes. PR7-O counts afresh: 1: PR7-O 0.1.0.
    constexpr int parameterVersion = 1;

    const juce::Identifier rigTag { "ParagonRig" };
    const juce::Identifier parameterTag { "Param" };
    const juce::Identifier instrumentTag { "Instrument" };
}

int Rig::numBanksWithExpansions()
{
    return numBanks + (int) expansions::list.size();
}

juce::String Rig::bankName (int bankIndex)
{
    if (isExpansionBank (bankIndex) && bankIndex < numBanksWithExpansions())
        return expansions::list[(size_t) (bankIndex - numBanks)].name;
    return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, numBanks - 1, bankIndex)));
}

juce::String Rig::getSlotName() const
{
    if (isExpansionBank (bank) && bank < numBanksWithExpansions())
        return juce::String (expansions::list[(size_t) (bank - numBanks)].code) + juce::String (number).paddedLeft ('0', 2);
    return bankName (bank) + juce::String (number).paddedLeft ('0', 2);
}

std::unique_ptr<juce::XmlElement> Rig::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (rigTag);
    xml->setAttribute ("version", fileVersion);
    xml->setAttribute ("parameterVersion", parameterVersion);
    xml->setAttribute ("name", name);
    xml->setAttribute ("bank", bankName (isExpansionBank (bank) ? 0 : bank));   // (0.13.0: files never hold an expansion bank)
    xml->setAttribute ("number", number);
    xml->setAttribute ("favourite", favourite);
    xml->setAttribute ("tags", tags.joinIntoString (","));
    if (notes.isNotEmpty())
        xml->setAttribute ("notes", notes);

    for (const auto& [id, value] : parameters)
    {
        auto* parameter = xml->createNewChildElement (parameterTag);
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }
    if (instrument.isNotEmpty())
        xml->createNewChildElement (instrumentTag)->setAttribute ("file", instrument);
    if (! seats.empty())
    {
        auto* stageXml = xml->createNewChildElement ("Stage");
        for (const auto& [name, seat] : seats)
        {
            auto* e = stageXml->createNewChildElement ("Seat");
            e->setAttribute ("name", name);
            e->setAttribute ("x", seat.x);
            e->setAttribute ("y", seat.y);
            e->setAttribute ("width", seat.width);
        }
    }
    if (! strips.empty() || ! sectionStrip.isPlain())   // 0.15.0
        xml->addChildElement (mixer::mixerXml (strips, sectionStrip).release());
    return xml;
}

std::optional<Rig> Rig::fromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (rigTag) || xml.getIntAttribute ("version", 0) > fileVersion)
        return std::nullopt;

    Rig rig;
    rig.name = xml.getStringAttribute ("name", "Untitled").trim();
    if (rig.name.isEmpty())
        rig.name = "Untitled";

    const auto bankLetter = xml.getStringAttribute ("bank", "A").toUpperCase();
    rig.bank = bankLetter.isEmpty() ? 0 : juce::jlimit (0, numBanks - 1, (int) (bankLetter[0] - 'A'));
    rig.number = juce::jlimit (1, maxNumber, xml.getIntAttribute ("number", 1));
    rig.favourite = xml.getBoolAttribute ("favourite");
    rig.tags = juce::StringArray::fromTokens (xml.getStringAttribute ("tags"), ",", {});
    rig.tags.trim();
    rig.tags.removeEmptyStrings();
    rig.notes = xml.getStringAttribute ("notes");

    for (const auto* parameter : xml.getChildWithTagNameIterator (parameterTag))
        if (const auto id = parameter->getStringAttribute ("id"); id.isNotEmpty())
            rig.parameters[id] = (float) parameter->getDoubleAttribute ("value");

    if (const auto* e = xml.getChildByName (instrumentTag))
        rig.instrument = e->getStringAttribute ("file");
    if (const auto* stageXml = xml.getChildByName ("Stage"))
        for (const auto* e : stageXml->getChildWithTagNameIterator ("Seat"))
            if (const auto name = e->getStringAttribute ("name"); name.isNotEmpty())
                rig.seats[name] = stage::clamp ({ (float) e->getDoubleAttribute ("x"), (float) e->getDoubleAttribute ("y"),
                                                  (float) e->getDoubleAttribute ("width", 3.0) });
    if (const auto* mixerXml = xml.getChildByName ("Mixer"))   // 0.15.0
        mixer::readMixerXml (*mixerXml, rig.strips, rig.sectionStrip);

    return rig;
}
