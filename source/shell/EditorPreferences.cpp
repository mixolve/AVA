#include "EditorPreferences.h"
#include "EditorStateSchema.h"

#include "Processor.h"

#include <array>

namespace
{
enum EditorPropertyPolicy { audioState = 0, viewState = 1, preservedPreference = 2 };
int editorPropertyPolicy(const juce::Identifier& property)
{
    auto policy = audioState;
    for (const auto* name : { AvaAudioProcessor::editorWidthStateKey, AvaAudioProcessor::editorHeightStateKey,
            AvaAudioProcessor::oscEnabledStateKey, AvaAudioProcessor::oscInputPortStateKey,
            AvaAudioProcessor::oscOutputHostStateKey, AvaAudioProcessor::oscOutputPortStateKey })
        if (property == juce::Identifier(name)) { policy = preservedPreference; break; }
    for (const auto* name : { "zoom_step", "editor_columns", "scroll.routing.x", "scroll.routing.y" })
        if (property == shell_editor_ui::key(name)) { policy = preservedPreference; break; }
    const auto name = property.toString();
    if (name.startsWith(shell_editor_ui::key("selected.").toString())
        || name.startsWith(shell_editor_ui::key("dock.meta.").toString())
        || property == shell_editor_ui::key("quick_access")
        || property == shell_editor_ui::key("pinned_routing_order")) policy = preservedPreference;
    if (property == juce::Identifier(AvaAudioProcessor::activeModuleStateKey)
        || property == juce::Identifier(AvaAudioProcessor::editorWidthStateKey)
        || property == juce::Identifier(AvaAudioProcessor::editorHeightStateKey)
        // Root history already omits A/B memories. Nested instances serialize
        // them too; initialising those memories on first open is not an edit.
        || property == juce::Identifier(AvaAudioProcessor::abCompareSnapshotAStateKey)
        || property == juce::Identifier(AvaAudioProcessor::abCompareSnapshotBStateKey)
        || property == juce::Identifier(AvaAudioProcessor::abCompareActiveSlotStateKey)
        || name.startsWith("ava.editor.ui.")
        || (name.startsWith("ava.editor.eql.band_") && name.endsWith(".filter_display_order"))
        || (name.startsWith("ava.crossover.")
            && (name.endsWith(".visible_band_index")
                || name.endsWith(".page_scroll_y") || name.endsWith(".has_ui_state"))))
        policy = static_cast<EditorPropertyPolicy>(policy | viewState);
    return policy;
}
}

void preserveEditorWindowState(juce::XmlElement& targetStateElement, const juce::ValueTree& sourceState)
{
    for (int index = targetStateElement.getNumAttributes(); --index >= 0;)
    {
        const auto name = targetStateElement.getAttributeName(index);
        if ((editorPropertyPolicy(juce::Identifier(name)) & preservedPreference) != 0 && !sourceState.hasProperty(name))
            targetStateElement.removeAttribute(name);
    }
    for (int index = 0; index < sourceState.getNumProperties(); ++index)
    {
        const auto property = sourceState.getPropertyName(index);
        if ((editorPropertyPolicy(property) & preservedPreference) != 0)
            targetStateElement.setAttribute(property.toString(), sourceState.getProperty(property).toString());
    }
}

bool isEditorViewProperty(const juce::Identifier& property)
{
    return (editorPropertyPolicy(property) & viewState) != 0;
}

namespace
{
struct EmbeddedState
{
    std::unique_ptr<juce::XmlElement> xml;
    bool binary = false;
};
EmbeddedState decodeEmbeddedState(const juce::String& value)
{
    if (value.trimStart().startsWithChar('<')) return { juce::XmlDocument::parse(value), false };
    const auto header = value.upToFirstOccurrenceOf(".", false, false);
    if (header.isEmpty() || !header.containsOnly("0123456789")
        || header.getLargeIntValue() < 8 || header.getLargeIntValue() > value.length()) return {};
    juce::MemoryBlock binary;
    if (value.length() >= 16 && binary.fromBase64Encoding(value))
        return { juce::AudioProcessor::getXmlFromBinary(binary.getData(), static_cast<int>(binary.getSize())), true };
    return {};
}

// Module states are binary XML attributes; routing contains XML with further
// binary instance states. Traverse both formats so nested windows obey history.
bool transformEditorViews(juce::XmlElement& target, const juce::XmlElement* current)
{
    bool changed = false;
    const auto canPreserveView = [&target](const juce::String& name, const juce::String& value)
    {
        if (name != AvaAudioProcessor::activeModuleStateKey) return true;
        const auto* key = value == AvaAudioProcessor::blockChainModuleId ? AvaAudioProcessor::blockChainModuleStateKey
            : value == AvaAudioProcessor::dynModuleId ? AvaAudioProcessor::dynModuleStateKey
            : value == AvaAudioProcessor::eqlModuleId ? AvaAudioProcessor::eqlModuleStateKey : nullptr;
        return key != nullptr && target.hasAttribute(key);
    };
    for (int i = target.getNumAttributes(); --i >= 0;)
    {
        const auto name = target.getAttributeName(i);
        if (current == nullptr && name.startsWith("ava.crossover.") && !target.getBoolAttribute(name)
            && (name.endsWith(".auto_solo_enabled") || name.endsWith(".manual_solo_inclusive")
                || name.contains(".manual_solo.")))
        { target.removeAttribute(name); changed = true; continue; }
        if (!isEditorViewProperty(juce::Identifier(name))) continue;
        if (current != nullptr && current->hasAttribute(name))
        {
            const auto value = current->getStringAttribute(name);
            if (!canPreserveView(name, value)) continue;
            if (target.getStringAttribute(name) != value) { target.setAttribute(name, value); changed = true; }
        }
        else { target.removeAttribute(name); changed = true; }
    }
    if (current != nullptr)
        for (int i = 0; i < current->getNumAttributes(); ++i)
        {
            const auto name = current->getAttributeName(i);
            if (isEditorViewProperty(juce::Identifier(name))
                && canPreserveView(name, current->getStringAttribute(name))
                && (!target.hasAttribute(name) || target.getStringAttribute(name) != current->getStringAttribute(name)))
            { target.setAttribute(name, current->getStringAttribute(name)); changed = true; }
        }
    for (int i = 0; i < target.getNumAttributes(); ++i)
    {
        const auto name = target.getAttributeName(i);
        const auto embedded = name == AvaAudioProcessor::eqlModuleStateKey
            || name == AvaAudioProcessor::blockChainModuleStateKey || name == AvaAudioProcessor::dynModuleStateKey
            || name == "ava.routing.processor_states" || name == "state"
            || name == AvaAudioProcessor::abCompareSnapshotAStateKey || name == AvaAudioProcessor::abCompareSnapshotBStateKey;
        if (!embedded) continue;
        auto nested = decodeEmbeddedState(target.getStringAttribute(name));
        if (nested.xml == nullptr) continue;
        auto source = current != nullptr ? decodeEmbeddedState(current->getStringAttribute(name)) : EmbeddedState {};
        if (!transformEditorViews(*nested.xml, source.xml.get())) continue;
        if (nested.binary)
        {
            juce::MemoryBlock encoded;
            juce::AudioProcessor::copyXmlToBinary(*nested.xml, encoded);
            target.setAttribute(name, encoded.toBase64Encoding());
        }
        else target.setAttribute(name, nested.xml->toString());
        changed = true;
    }
    const auto autoSolo = crossover_ui::makeStatePropertyName("crossover", "auto_solo_enabled");
    if (target.getBoolAttribute(autoSolo) && (current == nullptr || current->getBoolAttribute(autoSolo)))
        for (size_t band = 0; band < ava::crossover::Splitter::numRanges; ++band)
        {
            const auto id = AvaAudioProcessor::getCrossoverSoloParameterId(band);
            for (auto* parameter : target.getChildIterator())
                if (parameter->hasTagName("PARAM") && parameter->getStringAttribute("id") == id)
                {
                    auto value = juce::String("0");
                    if (current != nullptr)
                        for (const auto* source : current->getChildIterator())
                            if (source->hasTagName("PARAM") && source->getStringAttribute("id") == id)
                            { value = source->getStringAttribute("value"); break; }
                    if (parameter->getStringAttribute("value") != value)
                    { parameter->setAttribute("value", value); changed = true; }
                }
        }
    int index = 0;
    for (auto* child : target.getChildIterator())
    {
        const auto* source = current != nullptr ? current->getChildElement(index) : nullptr;
        if (current != nullptr && child->hasAttribute("id"))
        {
            source = nullptr;
            for (const auto* candidate : current->getChildIterator())
                if (candidate->hasTagName(child->getTagName())
                    && candidate->getStringAttribute("id") == child->getStringAttribute("id"))
                { source = candidate; break; }
        }
        if (source != nullptr && !source->hasTagName(child->getTagName())) source = nullptr;
        changed = transformEditorViews(*child, source) || changed;
        ++index;
    }
    return changed;
}
}

bool historySnapshotsMatchIgnoringViews(const juce::MemoryBlock& a, const juce::MemoryBlock& b)
{
    if (a == b) return true;
    const auto decode = [](const juce::MemoryBlock& data)
    { return juce::AudioProcessor::getXmlFromBinary(data.getData(), static_cast<int>(data.getSize())); };
    auto first = decode(a), second = decode(b);
    if (first == nullptr || second == nullptr) return false;
    transformEditorViews(*first, nullptr);
    transformEditorViews(*second, nullptr);
    return first->isEquivalentTo(second.get(), true);
}
void preserveEditorViewState(juce::XmlElement& target, const juce::XmlElement& current)
{
    transformEditorViews(target, &current);
}
