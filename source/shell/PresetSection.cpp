#include "Editor.h"
#include "PresetSection.h"

namespace
{
std::unique_ptr<PushControl> makeSectionButton(const juce::String& text)
{
    auto control = std::make_unique<PushControl>(text);
    control->setBottomIndicatorsVisible(false, false);
    return control;
}
}

AvaAudioProcessorEditor::PresetsSection::PresetsSection()
{
    presetChoice.setBottomIndicatorsVisible(false, false);
    presetChoice.getProperties().set("layoutCellSpan", 1);

    presetChoice.setName("preset-list");
    presetChoice.setIdleLabelVisible(true);
    presetChoice.displayFormatter = [](int, const juce::String& value)
    {
        return value.isEmpty() || value.equalsIgnoreCase("none") ? juce::String("PRESET\nSELECT") : value;
    };

    addButton = makeSectionButton("PRESET\nADD");
    saveButton = makeSectionButton("PRESET\nSAVE");
    renameButton = makeSectionButton("PRESET\nRENAME");
    defaultButton = makeSectionButton("PRESET\nDEFAULT");
    deleteButton = makeSectionButton("PRESET\nDELETE");
    copyFiltersButton = makeSectionButton("COPY");
    copyFiltersButton->setComponentID("ALL\nCOPY");
    pasteFilterButton = makeSectionButton("PASTE");
    pasteFilterButton->setComponentID("ALL\nPASTE");

    presetChoice.onValueChanged = [this]
    {
        if (ignorePresetCallbacks) return;
        const auto selectedIndex = presetChoice.getSelectedChoiceIndex();
        selectedPresetName = juce::isPositiveAndBelow(selectedIndex, presetNames.size())
            ? presetNames[selectedIndex] : juce::String {};
        if (onPresetSelected) onPresetSelected();
    };
}

void AvaAudioProcessorEditor::PresetsSection::beginRename()
{
    if (selectedPresetName.isEmpty() || onRenameRequested == nullptr)
        return;

    onRenameRequested(selectedPresetName);
}

juce::String AvaAudioProcessorEditor::PresetsSection::getSelectedPresetName() const
{
    return selectedPresetName;
}

void AvaAudioProcessorEditor::PresetsSection::setPresetNames(const juce::StringArray& names,
                                                             const juce::String& preferredSelection)
{
    const juce::ScopedValueSetter<bool> scopedIgnore(ignorePresetCallbacks, true);
    presetNames = names;
    juce::StringArray choices;
    for (const auto& name : names) choices.add(name.toUpperCase());
    presetChoice.setChoices(choices);
    presetChoice.setInteractionEnabled(!names.isEmpty());
    auto selectedIndex = -1;

    if (preferredSelection.isNotEmpty())
    {
        for (int index = 0; index < names.size(); ++index)
        {
            if (names[index].equalsIgnoreCase(preferredSelection))
            {
                selectedIndex = index;
                break;
            }
        }
    }

    if (selectedIndex >= 0) presetChoice.setSelectedChoiceIndex(selectedIndex, false);
    else
    {
        presetChoice.setChoices({});
        presetChoice.setInteractionEnabled(false);
    }
    selectedPresetName = juce::isPositiveAndBelow(selectedIndex, names.size())
        ? names[selectedIndex]
        : juce::String {};
}
