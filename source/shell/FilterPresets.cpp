#include "Editor.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/Presets.h"

namespace
{
struct NumberedNameSeed
{
    juce::String prefix;
    bool hasSeparatorBeforeNumber = false;
    int nextNumber = 2;
};

NumberedNameSeed makeNumberedNameSeed(const juce::String& name)
{
    const auto trimmedName = name.trim();
    auto splitIndex = trimmedName.length();

    while (splitIndex > 0 && juce::CharacterFunctions::isDigit(trimmedName[splitIndex - 1]))
        --splitIndex;

    if (splitIndex == trimmedName.length())
        return { trimmedName, true, 2 };

    const auto numberText = trimmedName.substring(splitIndex);
    const auto prefix = trimmedName.substring(0, splitIndex);

    return { prefix, false, numberText.getIntValue() + 1 };
}

juce::String buildNumberedName(const juce::String& prefix,
                               const int number,
                               const bool insertSeparator)
{
    if (prefix.isEmpty())
        return juce::String(number);

    return insertSeparator ? prefix + " " + juce::String(number)
                           : prefix + juce::String(number);
}

juce::String makeUniquePresetName(const juce::String& requestedName,
                                  const juce::StringArray& existingNames,
                                  const juce::String& fallbackPrefix)
{
    auto baseName = requestedName.trim();

    if (baseName.isEmpty())
    {
        auto nextIndex = existingNames.size() + 1;
        auto generatedName = fallbackPrefix + " " + juce::String(nextIndex);

        while (existingNames.contains(generatedName, true))
            generatedName = fallbackPrefix + " " + juce::String(++nextIndex);

        return generatedName;
    }

    if (! existingNames.contains(baseName, true))
        return baseName;

    const auto numberedSeed = makeNumberedNameSeed(baseName);
    auto candidateNumber = numberedSeed.nextNumber;
    auto candidate = buildNumberedName(numberedSeed.prefix, candidateNumber, numberedSeed.hasSeparatorBeforeNumber);

    while (existingNames.contains(candidate, true))
        candidate = buildNumberedName(numberedSeed.prefix,
                                      ++candidateNumber,
                                      numberedSeed.hasSeparatorBeforeNumber);

    return candidate;
}
}

void AvaAudioProcessorEditor::refreshFilterPresetList()
{
    if (presetsSection == nullptr)
        return;

    auto* eqlProcessor = getActiveEqlProcessor();
    const auto presetNames = eqlProcessor != nullptr ? eqlProcessor->getFilterPresetNames()
                                                     : juce::StringArray {};
    presetsSection->setPresetNames(presetNames,
        eqlProcessor != nullptr ? eqlProcessor->getSelectedFilterPresetName() : juce::String {});

    const auto hasPresetSelection = presetNames.size() > 0
        && presetsSection->getSelectedPresetName().isNotEmpty();
    const auto isNonePreset = presetsSection->getSelectedPresetName().equalsIgnoreCase(eql_presets::nonePresetName);
    const auto canDeletePreset = presetNames.size() > 1 && hasPresetSelection && !isNonePreset;
    presetsSection->deleteButton->setInteractionEnabled(canDeletePreset);
    presetsSection->deleteButton->setAlpha(1.0f);
    presetsSection->renameButton->setInteractionEnabled(hasPresetSelection && !isNonePreset);
    presetsSection->renameButton->setAlpha(1.0f);
    presetsSection->defaultButton->setInteractionEnabled(hasPresetSelection);
    presetsSection->defaultButton->setRestingState(hasPresetSelection && eqlProcessor != nullptr
        && presetsSection->getSelectedPresetName().equalsIgnoreCase(eqlProcessor->getDefaultFilterPresetName()));
    presetsSection->defaultButton->setAlpha(1.0f);
}

void AvaAudioProcessorEditor::reloadFilterPresetFromProcessor()
{
    if (presetsSection == nullptr)
        return;

    filterDisplayOrder.clear();
    filterDisplayOrder.reserve(EqlModuleProcessor::maxFilterCount);

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
        filterDisplayOrder.push_back(filterIndex);

    for (auto& sectionPtr : filterSections)
    {
        auto* section = sectionPtr.get();

        if (section == nullptr)
            continue;

        section->refreshTypeDependentControls();
    }

    enforceSingleExpandedFilterSection();
    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
}

void AvaAudioProcessorEditor::commitFilterDisplayOrderToProcessor()
{
    auto* eqlProcessor = getActiveEqlProcessor();
    const auto activeCount = getActiveFilterCount();

    if (eqlProcessor == nullptr || activeCount <= 1)
        return;

    if (static_cast<int>(filterDisplayOrder.size()) < activeCount)
        return;

    auto alreadyInOrder = true;
    std::vector<int> orderedFilterIndices;
    orderedFilterIndices.reserve(static_cast<size_t>(activeCount));

    for (int orderIndex = 0; orderIndex < activeCount; ++orderIndex)
    {
        const auto filterIndex = filterDisplayOrder[static_cast<size_t>(orderIndex)];
        orderedFilterIndices.push_back(filterIndex);

        if (filterIndex != orderIndex)
            alreadyInOrder = false;
    }

    if (alreadyInOrder || ! eqlProcessor->applyFilterOrder(orderedFilterIndices))
        return;

    filterDisplayOrder.clear();
    filterDisplayOrder.reserve(EqlModuleProcessor::maxFilterCount);

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
        filterDisplayOrder.push_back(filterIndex);

    for (auto& sectionPtr : filterSections)
    {
        auto* section = sectionPtr.get();

        if (section == nullptr)
            continue;

        section->refreshTypeDependentControls();
    }

    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
}

void AvaAudioProcessorEditor::addFilterPreset()
{
    if (presetsSection == nullptr)
        return;

    auto* eqlProcessor = getActiveEqlProcessor();
    if (eqlProcessor == nullptr) return;
    auto sourceName = presetsSection->getSelectedPresetName();
    if (sourceName.equalsIgnoreCase(eql_presets::nonePresetName)) sourceName.clear();
    const auto presetName = makeUniquePresetName(sourceName, eqlProcessor->getFilterPresetNames(), "PRESET");

    commitFilterDisplayOrderToProcessor();

    if (eqlProcessor != nullptr && eqlProcessor->saveFilterPreset(presetName))
        refreshFilterPresetList();
}

void AvaAudioProcessorEditor::saveFilterPreset()
{
    if (presetsSection == nullptr)
        return;

    auto presetName = presetsSection->getSelectedPresetName();
    auto* eqlProcessor = getActiveEqlProcessor();

    if (eqlProcessor == nullptr)
        return;

    if (presetName.isEmpty())
        presetName = makeUniquePresetName({}, eqlProcessor->getFilterPresetNames(), "PRESET");

    if (presetName.isEmpty())
        return;

    commitFilterDisplayOrderToProcessor();

    if (eqlProcessor->saveFilterPreset(presetName))
        refreshFilterPresetList();
}

bool AvaAudioProcessorEditor::renameFilterPreset(const juce::String& sourcePresetName, const juce::String& newPresetName)
{
    const auto trimmedSourceName = sourcePresetName.trim();
    const auto trimmedNewName = newPresetName.trim();

    if (trimmedSourceName.isEmpty() || trimmedNewName.isEmpty())
        return false;

    auto* eqlProcessor = getActiveEqlProcessor();

    if (eqlProcessor == nullptr || ! eqlProcessor->renameFilterPreset(trimmedSourceName, trimmedNewName))
        return false;

    refreshFilterPresetList();
    return true;
}

void AvaAudioProcessorEditor::setDefaultFilterPreset()
{
    if (presetsSection == nullptr)
        return;

    const auto presetName = presetsSection->getSelectedPresetName();

    if (presetName.isEmpty())
        return;

    if (auto* eqlProcessor = getActiveEqlProcessor(); eqlProcessor != nullptr && eqlProcessor->setDefaultFilterPreset(presetName))
        refreshFilterPresetList();
}

void AvaAudioProcessorEditor::deleteSelectedFilterPreset()
{
    if (presetsSection == nullptr)
        return;

    auto* eqlProcessor = getActiveEqlProcessor();

    if (eqlProcessor == nullptr || eqlProcessor->getFilterPresetNames().size() <= 1)
        return;

    const auto presetName = presetsSection->getSelectedPresetName();

    if (presetName.isEmpty())
        return;

    if (eqlProcessor->deleteFilterPreset(presetName))
    {
        reloadFilterPresetFromProcessor();
        refreshFilterPresetList();
        scheduleHistorySnapshot();
    }
}
