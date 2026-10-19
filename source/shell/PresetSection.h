#pragma once

#include "Editor.h"
#include "DisclosureAnimation.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"

struct AvaAudioProcessorEditor::PresetsSection
{
    PresetsSection();

    void beginRename();
    juce::String getSelectedPresetName() const;
    void setPresetNames(const juce::StringArray& names, const juce::String& preferredSelection);

    juce::Component presetClip, actionsClip;
    DisclosureAnimation actionsReveal;
    NoTickComboBox presetCombo;
    std::unique_ptr<BoxTextButton> actionsToggleButton;
    std::unique_ptr<BoxTextButton> addButton;
    std::unique_ptr<BoxTextButton> saveButton;
    std::unique_ptr<BoxTextButton> renameButton;
    std::unique_ptr<BoxTextButton> defaultButton;
    std::unique_ptr<BoxTextButton> deleteButton;
    bool ignorePresetCallbacks = false;
    juce::StringArray presetNames;
    juce::String selectedPresetName;
    std::function<void()> onPresetSelected;
    std::function<void(const juce::String&)> onRenameRequested;
};
