#include "ClipboardTransfer.h"
#include "Processor.h"
#include "StateValidation.h"

namespace ava::clipboard
{
namespace
{
constexpr auto pluginHeader = "AVA-PLUGIN-V1\n";
constexpr auto instanceHeader = "AVA-INSTANCE-ALL-V1\n";

juce::ValueTree decode(const juce::MemoryBlock& bytes)
{
    if (bytes.isEmpty() || bytes.getSize() > 64 * 1024 * 1024) return {};
    auto xml = juce::AudioProcessor::getXmlFromBinary(bytes.getData(), int(bytes.getSize()));
    return xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree {};
}

bool readClipboard(const char* header, juce::MemoryBlock& bytes)
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    if (!text.startsWith(header)) return false;
    const auto encoded = text.substring(juce::String(header).length());
    // Bounds and format are checked before touching any DSP or host state.
    if (encoded.isEmpty() || encoded.length() > 90 * 1024 * 1024) return false;
    return bytes.fromBase64Encoding(encoded) && bytes.getSize() > 0 && bytes.getSize() <= 64 * 1024 * 1024;
}

bool writeClipboard(AvaAudioProcessor& processor, const char* header)
{
    juce::MemoryBlock bytes;
    processor.getStateInformation(bytes);
    if (bytes.isEmpty()) return false;
    juce::SystemClipboard::copyTextToClipboard(juce::String(header) + bytes.toBase64Encoding());
    return true;
}

bool isInstanceProperty(const juce::String& name)
{
    // Only transfer this instance's audio/UI settings. In particular never
    // replace the destination's routing graph, IDs, names, OSC, host slots,
    // editor window size, zoom, or A/B snapshots through INSTANCE/ALL.
    return name == AvaAudioProcessor::activeModuleStateKey
        || name == AvaAudioProcessor::eqlModuleStateKey
        || name == AvaAudioProcessor::dynModuleStateKey
        || name == AvaAudioProcessor::blockChainModuleStateKey
        || name.startsWith("ava.block_order.")
        || name.startsWith("ava.block_hidden.")
        || name.startsWith("ava.module_bypass.")
        || name.startsWith("ava.crossover.")
        || name.startsWith("ava.editor.eql.band_")
        || name.startsWith("ava.editor.ui.eql.expanded.")
        || name.startsWith("ava.editor.ui.collapsed.");
}

void mergeInstanceSettings(const juce::ValueTree& source, juce::ValueTree& target)
{
    // Source and target are private snapshots; no live state changes happen
    // until the fully assembled destination snapshot passes validation.
    for (int index = target.getNumProperties() - 1; index >= 0; --index)
    {
        const auto property = target.getPropertyName(index);
        if (isInstanceProperty(property.toString())) target.removeProperty(property, nullptr);
    }
    for (int index = 0; index < source.getNumProperties(); ++index)
    {
        const auto property = source.getPropertyName(index);
        if (isInstanceProperty(property.toString()))
            target.setProperty(property, source.getProperty(property), nullptr);
    }

    // APVTS serialises each parameter as a child identified by "id".
    // Copy the audio parameters, not the 64 host automation slot proxies.
    for (const auto sourceChild : source)
    {
        if (!sourceChild.hasProperty("id") || !sourceChild.hasProperty("value")) continue;
        const auto id = sourceChild.getProperty("id").toString();
        if (id.startsWith(AvaAudioProcessor::paramHostSlotPrefix)) continue;
        for (auto targetChild : target)
            if (targetChild.getProperty("id").toString() == id)
            {
                targetChild.setProperty("value", sourceChild.getProperty("value"), nullptr);
                break;
            }
    }
}

bool restore(AvaAudioProcessor& processor, const juce::ValueTree& state)
{
    if (!state.isValid() || !shell_state_validation::hasCurrentState(state, processor, true)) return false;
    const auto xml = state.createXml();
    if (xml == nullptr) return false;
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(*xml, bytes);
    // Preserve destination A/B memories and routing state on INSTANCE/ALL;
    // the full-plugin format, by contrast, replaces absolutely everything.
    if (!processor.applyClipboardStateInformation(bytes.getData(), int(bytes.getSize()))) return false;
    processor.notifyHostOfStateChange();
    return true;
}
}

bool copyPlugin(AvaAudioProcessor& processor) { return writeClipboard(processor, pluginHeader); }

bool pastePlugin(AvaAudioProcessor& processor)
{
    juce::MemoryBlock bytes;
    if (!readClipboard(pluginHeader, bytes)) return false;
    return restore(processor, decode(bytes));
}

bool copyInstanceAll(AvaAudioProcessor& processor) { return writeClipboard(processor, instanceHeader); }

bool pasteInstanceAll(AvaAudioProcessor& processor)
{
    juce::MemoryBlock sourceBytes;
    if (!readClipboard(instanceHeader, sourceBytes)) return false;
    const auto source = decode(sourceBytes);
    if (!source.isValid() || source.getType() != processor.getValueTreeState().state.getType()) return false;

    juce::MemoryBlock destinationBytes;
    processor.getStateInformation(destinationBytes);
    auto destination = decode(destinationBytes);
    if (!destination.isValid()) return false;
    mergeInstanceSettings(source, destination);
    return restore(processor, destination);
}
}
