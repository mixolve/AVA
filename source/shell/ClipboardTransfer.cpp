#include "ClipboardTransfer.h"
#include "Processor.h"
#include "../crossover/ParameterIds.h"
#include "../modules/dyn/Processor.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/shared/BlockParameters.h"

#include <map>

namespace ava::clipboard
{
namespace
{
constexpr auto pluginHeader = "AVA-PLUGIN-V1\n";
constexpr auto instanceHeader = "AVA-INSTANCE-ALL-V1\n";
constexpr auto moduleHeader = "AVA-MODULE-V1\n";
const juce::Identifier moduleSettingsType {"module_settings"};

struct ModuleSettingsTarget
{
    juce::AudioProcessorValueTreeState* state = nullptr;
    BlockChainProcessor* chain = nullptr;
    int type = -1;
    const char* blockName = nullptr;
    const char* hiddenSuffix = nullptr;
};

ModuleSettingsTarget getModuleSettingsTarget(AvaAudioProcessor& processor, size_t band, int uid)
{
    const auto stack = processor.getModuleStackForRange(band);
    const auto* slot = ava::module_stack::find(stack, uid);
    if (slot == nullptr || !slot->shown) return {};
    const auto isCopy = uid >= ava::module_stack::originalCount;
    if (module_order::isChain(slot->type))
    {
        auto* chain = isCopy ? processor.getCopiedBlockChainProcessor(uid) : processor.getBlockChainProcessor();
        if (chain == nullptr) return {};
        const auto& block = ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(slot->type))];
        return {&chain->getValueTreeState(), chain, slot->type,
            block.name, block.hiddenSuffix};
    }
    if (slot->type == module_order::dyn)
    {
        auto* dyn = isCopy ? processor.getCopiedDynProcessor(uid) : processor.getDynModuleProcessor();
        if (dyn != nullptr) return {&dyn->getValueTreeState(), nullptr, slot->type, {}, {}};
    }
    return {};
}

std::map<juce::String, juce::RangedAudioParameter*> getModuleParameters(
    const ModuleSettingsTarget& target, size_t band)
{
    std::map<juce::String, juce::RangedAudioParameter*> parameters;
    const auto addParameter = [&](const juce::String& suffix)
    {
        if (auto* parameter = target.state->getParameter(
                ava::crossover::parameters::makeRangeParameterId(band, suffix.toRawUTF8())))
            parameters.emplace(suffix, parameter);
    };
    if (target.chain != nullptr)
    {
        using namespace ava::modules::chain::parameters;
        const auto blockName = juce::String(target.blockName);
        const auto hidden = juce::String(target.hiddenSuffix);
        for (size_t index = 0; index < parameterSpecs.size(); ++index)
            if (blockName == hostLabels[index].block && hidden != parameterSpecs[index].suffix)
                addParameter(parameterSpecs[index].suffix);
    }
    else
    {
        const auto bandPrefix = ava::crossover::parameters::makeRangeParameterId(band, "");
        for (const auto child : target.state->copyState())
        {
            const auto id = child.getProperty("id").toString();
            if (id.startsWith(bandPrefix)) addParameter(id.substring(bandPrefix.length()));
        }
    }
    return parameters;
}

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
    if (!state.isValid() || state.getType() != processor.getValueTreeState().state.getType()) return false;
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

bool copyModule(AvaAudioProcessor& processor, size_t band, int uid)
{
    const auto target = getModuleSettingsTarget(processor, band, uid);
    if (target.state == nullptr) return false;
    const auto parameters = getModuleParameters(target, band);
    if (parameters.empty()) return false;

    juce::ValueTree settings(moduleSettingsType);
    settings.setProperty("type", target.type, nullptr);
    for (const auto& [suffix, parameter] : parameters)
    {
        juce::ValueTree entry("parameter");
        entry.setProperty("id", suffix, nullptr);
        entry.setProperty("normalized", double(parameter->getValue()), nullptr);
        settings.appendChild(entry, nullptr);
    }
    if (target.type == int(ava::modules::chain::blocks::Id::gain))
        settings.setProperty("gain_order", ava::modules::chain::gain::serialise(target.chain->getGainOrder(band)), nullptr);

    auto xml = settings.createXml();
    if (xml == nullptr) return false;
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(*xml, bytes);
    juce::SystemClipboard::copyTextToClipboard(juce::String(moduleHeader) + bytes.toBase64Encoding());
    return true;
}

bool pasteModule(AvaAudioProcessor& processor, size_t band, int uid)
{
    juce::MemoryBlock bytes;
    if (!readClipboard(moduleHeader, bytes)) return false;
    const auto settings = decode(bytes);
    const auto target = getModuleSettingsTarget(processor, band, uid);
    if (target.state == nullptr || !settings.hasType(moduleSettingsType)
        || settings.getProperty("type").toString() != juce::String(target.type)) return false;

    auto parameters = getModuleParameters(target, band);
    if (parameters.empty() || size_t(settings.getNumChildren()) != parameters.size()) return false;
    std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
    values.reserve(parameters.size());
    for (const auto entry : settings)
    {
        const auto found = parameters.find(entry.getProperty("id").toString());
        const auto value = ava::modules::state::parseExactFiniteNumber(entry.getProperty("normalized"));
        if (!entry.hasType("parameter") || found == parameters.end()
            || !value.has_value() || *value < 0.0 || *value > 1.0)
            return false;
        values.emplace_back(found->second, float(*value));
        parameters.erase(found);
    }
    ava::modules::chain::gain::Order gainOrder {};
    const bool isGain = target.type == int(ava::modules::chain::blocks::Id::gain);
    if (isGain && !ava::modules::chain::gain::parse(settings.getProperty("gain_order").toString(), gainOrder))
        return false;

    for (const auto& [parameter, value] : values)
    {
        if (juce::approximatelyEqual(parameter->getValue(), value)) continue;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(value);
        parameter->endChangeGesture();
    }
    if (isGain) target.chain->setGainOrder(band, gainOrder);
    return true;
}
}
