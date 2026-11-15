#pragma once

#include "Editor.h"
#include "FilterSection.h"
#include "PresetSection.h"

// Complete, parameter-bound EQL editor belonging to ONE copied module.
// No filter controls or fold state are shared with the original or other copies.
class AvaAudioProcessorEditor::EqlCopyEditor final : public ControlBlockComponent
{
public:
    EqlCopyEditor(AvaAudioProcessorEditor& owner, EqlModuleProcessor& processor,
                  int uid, int band);
    ~EqlCopyEditor() override;

    void updateLayout(int width);
    EqlModuleProcessor* getProcessorIdentity() const noexcept { return &processor; }
    int getBand() const noexcept { return band; }
    void refreshFromProcessor();

private:
    void initializeControls();
    void refreshFilterControls();
    void refreshPresets();
    void selectFilter(int filterIndex);
    void addFilter();
    void deleteFilter();
    void sortFilters();
    void copyFilter();
    void pasteFilter();
    void copyAllFilters();
    void pasteAllFilters();
    void rememberOrder();
    void restoreOrder();
    void notifyChange();
    void saveFilterPreset();
    void addFilterPreset();
    void renameFilterPreset();
    void loadFilterPreset();
    void deleteFilterPreset();

    AvaAudioProcessorEditor& owner;
    EqlModuleProcessor& processor;
    const int uid;
    const int band;
    int currentFilter = 0;
    int lastActiveCount = -1;
    int currentWidth = 0;
    bool updating = false;
    std::vector<int> displayOrder;
    std::array<std::unique_ptr<FilterSection>, EqlModuleProcessor::maxFilterCount> filters;
    std::unique_ptr<PresetsSection> presets;
    std::unique_ptr<PushControl> sortButton;
    std::unique_ptr<PushControl> copyButton;
    std::unique_ptr<PushControl> pasteButton;
    std::unique_ptr<PushControl> deleteButton;
    std::unique_ptr<PushControl> addButton;
};
