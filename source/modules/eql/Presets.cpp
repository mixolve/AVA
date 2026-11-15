#include "PresetStorage.h"
#include "Presets.h"
#include "StateValidation.h"

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
void ensureDefaultPresetExists()
{
    const PresetCatalogTransaction transaction;
    if (!transaction) return;
    auto presetsXml = loadFilterPresetsXml();

    if (presetsXml == nullptr)
        presetsXml = createEmptyFilterPresetsXml();

    auto changed = false;
    if (findPresetElement(*presetsXml, nonePresetName) == nullptr)
    {
        // Build the fallback from fresh defaults, never from an edited band.
        auto initialProcessor = std::make_unique<EqlModuleProcessor>();
        auto stateXml = copySerializableState(*initialProcessor).createXml();
        if (stateXml == nullptr) return;
        auto* preset = presetsXml->createNewChildElement(presetTag);
        preset->setAttribute("name", nonePresetName);
        preset->addChildElement(stateXml.release());
        changed = true;
    }
    if (getStoredDefaultPresetName(*presetsXml).isEmpty())
    {
        selectStoredDefaultPreset(*presetsXml, nonePresetName);
        changed = true;
    }
    if (changed) writeFilterPresetsXml(*presetsXml);
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
    // This is the state actually applied, including edits after loading it.
    return parameters.state.getProperty(filterPresetSelectedStateKey).toString();
}

juce::StringArray EqlModuleProcessor::getFilterPresetNames()
{
    juce::StringArray presetNames;
    eql_presets::ensureDefaultPresetExists();
    auto presetsXml = loadFilterPresetsXml();
    resolveFilterPresetSelection(presetsXml.get());

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
    const auto defaultName = getStoredDefaultPresetName(*presetsXml);
    for (int index = 0; index < presetNames.size(); ++index)
        if (presetNames[index].equalsIgnoreCase(defaultName))
        {
            presetNames.move(index, 0);
            break;
        }
    return presetNames;
}

bool EqlModuleProcessor::saveFilterPreset(const juce::String& presetName)
{
    const PresetCatalogTransaction transaction;
    if (!transaction) return false;
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

    auto stateXml = copySerializableState(*this).createXml();

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
    const PresetCatalogTransaction transaction;
    if (!transaction) return false;
    const auto sourceName = sourcePresetName.trim();
    const auto targetName = newPresetName.trim();

    if (sourceName.isEmpty() || targetName.isEmpty()
        || sourceName.equalsIgnoreCase(eql_presets::nonePresetName))
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
    const PresetCatalogTransaction transaction;
    if (!transaction) return false;
    const auto trimmedName = presetName.trim();

    if (trimmedName.isEmpty())
        return false;

    auto presetsXml = loadFilterPresetsXml();
    return presetsXml != nullptr
        && selectStoredDefaultPreset(*presetsXml, trimmedName)
        && writeFilterPresetsXml(*presetsXml);
}

bool EqlModuleProcessor::resolveFilterPresetSelection(const juce::XmlElement* catalog)
{
    if (catalog == nullptr) return false;
    const auto selected = getSelectedFilterPresetName();
    for (const auto* preset : catalog->getChildIterator())
        if (preset->hasTagName(presetTag) && preset->getStringAttribute("name").equalsIgnoreCase(selected))
            return false;
    const auto fallback = getStoredDefaultPresetName(*catalog);
    // A missing library entry changes its label, never the restored/edited sound.
    parameters.state.setProperty(filterPresetSelectedStateKey,
        fallback.isNotEmpty() ? fallback : juce::String(eql_presets::nonePresetName), nullptr);
    return true;
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

    if (! restoreState(restoredState, false))
        return false;

    parameters.state.setProperty(filterPresetSelectedStateKey, trimmedName, nullptr);
    return true;
}

bool EqlModuleProcessor::deleteFilterPreset(const juce::String& presetName)
{
    const PresetCatalogTransaction transaction;
    if (!transaction) return false;
    const auto trimmedName = presetName.trim();

    if (trimmedName.isEmpty() || trimmedName.equalsIgnoreCase(eql_presets::nonePresetName))
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
        replacementDefault = findPresetElement(*presetsXml, eql_presets::nonePresetName) != nullptr
            ? juce::String(eql_presets::nonePresetName)
            : firstPresetName(*presetsXml);

        if (replacementDefault.isNotEmpty())
            selectStoredDefaultPreset(*presetsXml, replacementDefault);
    }

    if (! writeFilterPresetsXml(*presetsXml))
        return false;

    if (selectedPreset.equalsIgnoreCase(trimmedName))
        return loadFilterPreset(replacementDefault);

    return true;
}
