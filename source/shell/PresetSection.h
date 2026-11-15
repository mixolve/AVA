#pragma once

#include "Editor.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"

struct AvaAudioProcessorEditor::PresetsSection
{
    PresetsSection();

    void beginRename();
    juce::String getSelectedPresetName() const;
    void setPresetNames(const juce::StringArray& names, const juce::String& preferredSelection);

    LocalChoiceControl presetChoice { "PRESET\nSELECT", juce::StringArray { "DEFAULT" }, 0 };
    std::unique_ptr<PushControl> addButton;
    std::unique_ptr<PushControl> saveButton;
    std::unique_ptr<PushControl> renameButton;
    std::unique_ptr<PushControl> defaultButton;
    std::unique_ptr<PushControl> deleteButton;
    std::unique_ptr<PushControl> copyFiltersButton;
    std::unique_ptr<PushControl> pasteFilterButton;
    bool ignorePresetCallbacks = false;
    juce::StringArray presetNames;
    juce::String selectedPresetName;
    std::function<void()> onPresetSelected;
    std::function<void(const juce::String&)> onRenameRequested;
};
