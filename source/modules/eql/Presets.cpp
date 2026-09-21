#include "PresetStorage.h"
#include "Presets.h"
#include "State.h"

namespace
{
inline constexpr auto defaultPresetAttribute = "is_default";

juce::String getStoredDefaultPresetName(const juce::XmlElement& presetsXml)
{
    juce::String defaultPresetName;

    for (auto* child : presetsXml.getChildIterator())
    {
        if (! child->hasTagName(presetTag) || ! child->getBoolAttribute(defaultPresetAttribute))
            continue;

        const auto presetName = child->getStringAttribute("name").trim();

        if (presetName.isEmpty() || defaultPresetName.isNotEmpty())
            return {};

        defaultPresetName = presetName;
    }

    return defaultPresetName;
}

bool selectStoredDefaultPreset(juce::XmlElement& presetsXml, const juce::String& presetName)
{
    auto found = false;

    for (auto* child : presetsXml.getChildIterator())
    {
        if (! child->hasTagName(presetTag))
            continue;

        const auto selected = child->getStringAttribute("name").trim().equalsIgnoreCase(presetName);

        if (selected)
        {
            child->setAttribute(defaultPresetAttribute, true);
            found = true;
        }
        else
        {
            child->removeAttribute(defaultPresetAttribute);
        }
    }

    return found;
}

int countPresets(const juce::XmlElement& presetsXml)
{
    auto count = 0;

    for (auto* child : presetsXml.getChildIterator())
        if (child->hasTagName(presetTag))
            ++count;

    return count;
}

juce::String firstPresetName(const juce::XmlElement& presetsXml)
{
    for (auto* child : presetsXml.getChildIterator())
    {
        if (! child->hasTagName(presetTag))
            continue;

        const auto name = child->getStringAttribute("name").trim();

        if (name.isNotEmpty())
            return name;
    }

    return {};
}

}

namespace eql_presets
{
void ensureDefaultPresetExists(EqlModuleProcessor& processor)
{
    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml != nullptr && getStoredDefaultPresetName(*presetsXml).isNotEmpty())
        return;

    if (presetsXml != nullptr && findPresetElement(*presetsXml, "default") != nullptr)
    {
        processor.setDefaultFilterPreset("default");
        return;
    }

    if (processor.saveFilterPreset("default"))
        processor.setDefaultFilterPreset("default");
}
}

juce::String EqlModuleProcessor::getDefaultFilterPresetName() const
{
    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml == nullptr || ! presetsXml->hasTagName(filterPresetsRootTag))
        return {};

    const auto storedDefault = getStoredDefaultPresetName(*presetsXml);
    return storedDefault.isNotEmpty() && findPresetElement(*presetsXml, storedDefault) != nullptr
        ? storedDefault
        : juce::String {};
}

juce::String EqlModuleProcessor::getSelectedFilterPresetName() const
{
    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml == nullptr || ! presetsXml->hasTagName(filterPresetsRootTag))
        return {};

    const auto selectedPreset = parameters.state.getProperty(filterPresetSelectedStateKey).toString().trim();

    if (selectedPreset.isNotEmpty() && findPresetElement(*presetsXml, selectedPreset) != nullptr)
        return selectedPreset;

    return {};
}

juce::StringArray EqlModuleProcessor::getFilterPresetNames() const
{
    juce::StringArray presetNames;
    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml == nullptr)
        return presetNames;

    for (auto* child : presetsXml->getChildIterator())
    {
        if (! child->hasTagName(presetTag))
            continue;

        const auto presetName = child->getStringAttribute("name").trim();

        if (presetName.isNotEmpty())
            presetNames.addIfNotAlreadyThere(presetName);
    }

    presetNames.sort(true);
    return presetNames;
}

bool EqlModuleProcessor::saveFilterPreset(const juce::String& presetName)
{
    const auto trimmedName = presetName.trim();

    if (trimmedName.isEmpty())
        return false;

    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml == nullptr || ! presetsXml->hasTagName(filterPresetsRootTag))
        presetsXml = createEmptyFilterPresetsXml();

    auto wasDefaultPreset = false;

    if (auto* existingPreset = findPresetElement(*presetsXml, trimmedName))
    {
        wasDefaultPreset = existingPreset->getBoolAttribute(defaultPresetAttribute);
        presetsXml->removeChildElement(existingPreset, true);
    }

    auto* presetElement = presetsXml->createNewChildElement(presetTag);
    presetElement->setAttribute("name", trimmedName);

    if (wasDefaultPreset)
        presetElement->setAttribute(defaultPresetAttribute, true);

    auto stateXml = createSerializableStateXml(*this);

    if (stateXml == nullptr)
        return false;

    presetElement->addChildElement(stateXml.release());

    if (! writeFilterPresetsXml(*presetsXml))
        return false;

    parameters.state.setProperty(filterPresetSelectedStateKey, trimmedName, nullptr);
    return true;
}

bool EqlModuleProcessor::renameFilterPreset(const juce::String& sourcePresetName,
                                            const juce::String& newPresetName)
{
    const auto sourceName = sourcePresetName.trim();
    const auto targetName = newPresetName.trim();

    if (sourceName.isEmpty() || targetName.isEmpty())
        return false;

    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml == nullptr || ! presetsXml->hasTagName(filterPresetsRootTag))
        return false;

    auto* sourcePresetElement = findPresetElement(*presetsXml, sourceName);

    if (sourcePresetElement == nullptr)
        return false;

    if (auto* targetPresetElement = findPresetElement(*presetsXml, targetName);
        targetPresetElement != nullptr && targetPresetElement != sourcePresetElement)
        return false;

    const auto selectedPreset = parameters.state.getProperty(filterPresetSelectedStateKey).toString().trim();
    sourcePresetElement->setAttribute("name", targetName);

    if (! writeFilterPresetsXml(*presetsXml))
        return false;

    if (selectedPreset.equalsIgnoreCase(sourceName))
        parameters.state.setProperty(filterPresetSelectedStateKey, targetName, nullptr);

    return true;
}

bool EqlModuleProcessor::setDefaultFilterPreset(const juce::String& presetName)
{
    const auto trimmedName = presetName.trim();

    if (trimmedName.isEmpty())
        return false;

    auto presetsXml = loadFilterPresetsXml();
    return presetsXml != nullptr
        && selectStoredDefaultPreset(*presetsXml, trimmedName)
        && writeFilterPresetsXml(*presetsXml);
}

bool EqlModuleProcessor::loadInitialFilterPreset() noexcept
{
    const auto defaultPresetName = getDefaultFilterPresetName();
    return defaultPresetName.isNotEmpty() && loadFilterPreset(defaultPresetName);
}

bool EqlModuleProcessor::loadFilterPreset(const juce::String& presetName) noexcept
{
    const auto trimmedName = presetName.trim();

    if (trimmedName.isEmpty())
        return false;

    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml == nullptr)
        return false;

    auto* presetElement = findPresetElement(*presetsXml, trimmedName);

    if (presetElement == nullptr)
        return false;

    auto* stateElement = presetElement->getChildByName(parameters.state.getType().toString());

    if (stateElement == nullptr)
        return false;

    auto restoredState = juce::ValueTree::fromXml(*stateElement);

    if (! restoreState(restoredState))
        return false;

    parameters.state.setProperty(filterPresetSelectedStateKey, trimmedName, nullptr);
    return true;
}

bool EqlModuleProcessor::deleteFilterPreset(const juce::String& presetName)
{
    const auto trimmedName = presetName.trim();

    if (trimmedName.isEmpty())
        return false;

    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml == nullptr || countPresets(*presetsXml) <= 1)
        return false;

    auto* presetElement = findPresetElement(*presetsXml, trimmedName);

    if (presetElement == nullptr)
        return false;

    const auto removedDefault = presetElement->getBoolAttribute(defaultPresetAttribute);
    const auto selectedPreset = parameters.state.getProperty(filterPresetSelectedStateKey).toString().trim();
    presetsXml->removeChildElement(presetElement, true);

    auto replacementDefault = getStoredDefaultPresetName(*presetsXml);

    if (removedDefault)
    {
        replacementDefault = findPresetElement(*presetsXml, "default") != nullptr
            ? juce::String("default")
            : firstPresetName(*presetsXml);

        if (replacementDefault.isNotEmpty())
            selectStoredDefaultPreset(*presetsXml, replacementDefault);
    }

    if (! writeFilterPresetsXml(*presetsXml))
        return false;

    if (selectedPreset.equalsIgnoreCase(trimmedName))
        parameters.state.removeProperty(filterPresetSelectedStateKey, nullptr);

    return true;
}
