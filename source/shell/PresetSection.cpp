#include "Editor.h"
#include "PresetSection.h"

namespace
{
void configurePresetCombo(NoTickComboBox& combo)
{
    combo.setEditableText(false);
    combo.setJustificationType(juce::Justification::centred);
    combo.setPopupMenuTextJustification(juce::Justification::centred);
    combo.setColour(juce::ComboBox::backgroundColourId, uiGreyDark);
    combo.setColour(juce::ComboBox::outlineColourId, uiGreyLight);
    combo.setColour(juce::ComboBox::textColourId, uiWhite);
    combo.setColour(juce::ComboBox::arrowColourId, uiWhite);
    combo.setColour(juce::ComboBox::buttonColourId, uiGreyDark);
    combo.setWantsKeyboardFocus(false);
    combo.setMouseClickGrabsKeyboardFocus(false);
    combo.setPromptStylePopupEnabled(true);
}

std::unique_ptr<BoxTextButton> makeSectionButton(const juce::String& text, const juce::Colour accent)
{
    auto button = std::make_unique<BoxTextButton>(accent);
    button->setButtonText(text);
    return button;
}
}

AvaAudioProcessorEditor::PresetsSection::PresetsSection()
{
    configurePresetCombo(presetCombo);

    presetClip.setName("presets");
    actionsClip.setName("preset-actions");
    presetCombo.setName("preset-list");

    actionsToggleButton = makeSectionButton({}, uiAccent);
    actionsToggleButton->setName("presets-toggle");
    actionsToggleButton->setTablerIcon("arrows-down");
    actionsToggleButton->setClickingTogglesState(true);
    actionsToggleButton->setToggleActiveFillEnabled(false);
    addButton = makeSectionButton("ADD", uiAccent);
    saveButton = makeSectionButton("SAVE", uiGreyLight);
    renameButton = makeSectionButton("RENAME", uiGreyLight);
    defaultButton = makeSectionButton("DEFAULT", uiAccent);
    deleteButton = makeSectionButton("DELETE", uiAccent);

    auto handlePresetSelection = [this]
    {
        const auto selectedIndex = presetCombo.getSelectedItemIndex();
        selectedPresetName = juce::isPositiveAndBelow(selectedIndex, presetNames.size())
            ? presetNames[selectedIndex]
            : juce::String {};

        if (onPresetSelected)
            onPresetSelected();
    };

    presetCombo.onChange = [this, handlePresetSelection]
    {
        if (ignorePresetCallbacks)
            return;

        if (presetCombo.getSelectedItemIndex() < 0)
            return;

        handlePresetSelection();
    };

    presetCombo.onReselectedCurrentItem = handlePresetSelection;
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
    presetCombo.clear(juce::dontSendNotification);

    for (int index = 0; index < names.size(); ++index)
        presetCombo.addItem(names[index].toUpperCase(), index + 1);

    if (names.isEmpty())
    {
        presetCombo.setText({}, juce::dontSendNotification);
        selectedPresetName.clear();
        return;
    }

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

    presetCombo.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
    selectedPresetName = juce::isPositiveAndBelow(selectedIndex, names.size())
        ? names[selectedIndex]
        : juce::String {};
}
