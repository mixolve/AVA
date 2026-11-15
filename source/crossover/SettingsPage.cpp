#include "Editor.h"
#include "Page.h"
#include "UiSupport.h"
#include "../shell/ChoiceControl.h"
#include "../shell/ParameterControl.h"

using namespace crossover_ui;

class CrossoverSettingsPage final : public CrossoverModulePage
{
public:
    explicit CrossoverSettingsPage(CrossoverModuleComponent& ownerIn) : owner(ownerIn)
    {
        // The CROSSOVER and INPUT controls are now one uninterrupted grid.
        // No nested dark #444444 section headers or collapsed controls.
        if (!owner.config.showCrossoverControls) return;
        if (owner.config.processingInputParameterId.isNotEmpty())
        {
            processingInputControl = std::make_unique<ChoiceControl>(owner.valueTreeState,
                owner.config.processingInputParameterId, "CHANNEL INPUT");
            processingInputControl->setBottomIndicatorsVisible(false, false);
            addAndMakeVisible(*processingInputControl);
        }
        const auto makeParameter = [this](const juce::String& id, const char* label)
        {
            auto control = std::make_unique<ParameterControl>(owner.valueTreeState, id, label, 0);
            control->setBottomIndicatorsVisible(false, false);
            addAndMakeVisible(*control);
            return control;
        };
        splitCountControl = makeParameter(owner.config.makeCrossoverSplitCountParameterId(), "XOVERS");
        splitCountControl->setDiscreteChoiceIndicator(static_cast<int>(splitControls.size()) + 1);
        splitCountControl->onValueChanged = [this] { owner.activeSplitCountChanged(); };
        if (owner.config.soloInstanceParameterId.isNotEmpty())
            soloInstanceControl = makeParameter(owner.config.soloInstanceParameterId, "INSTANCE SOLO");
        if (owner.config.soloChannelParameterId.isNotEmpty())
            soloChannelControl = makeParameter(owner.config.soloChannelParameterId, "CHANNEL SOLO");
        autoSoloControl = makeParameter(owner.config.makeCrossoverParameterId("auto-solo"), "AUTO-SOLO");
        autoSoloControl->onValueChanged = [this]
        {
            owner.setAutoSoloEnabled(autoSoloControl->getValue() >= 0.5);
        };
        soloModeControl = std::make_unique<LocalChoiceControl>("SOLO-MODE",
            juce::StringArray { "EXCL", "INCL" }, 0);
        soloModeControl->setBottomIndicatorsVisible(false, false);
        soloModeControl->onValueChanged = [this]
        {
            owner.setManualSoloInclusive(soloModeControl->getSelectedChoiceIndex() == 1);
        };
        addAndMakeVisible(*soloModeControl);
        for (size_t index = 0; index < splitControls.size(); ++index)
        {
            auto control = std::make_unique<ParameterControl>(owner.valueTreeState,
                owner.config.makeCrossoverParameterId(splitParameterSuffixes[index]),
                splitLabels[index], owner.config.crossoverDecimals);
            control->setBottomIndicatorsVisible(false, false);
            addAndMakeVisible(*control);
            splitControls[index] = std::move(control);
        }
        if (owner.config.copyInstanceAll && owner.config.pasteInstanceAll)
        {
            copyAllControl = std::make_unique<PushControl>("COPY-ALL");
            pasteAllControl = std::make_unique<PushControl>("PASTE-ALL");
            for (auto* button : { copyAllControl.get(), pasteAllControl.get() })
            {
                button->setBottomIndicatorsVisible(false, false);
                addAndMakeVisible(*button);
            }
            copyAllControl->onTrigger = [this]
            {
                if (owner.config.copyInstanceAll) owner.config.copyInstanceAll();
                owner.clearFocus();
            };
            pasteAllControl->onTrigger = [this]
            {
                if (owner.config.pasteInstanceAll) owner.config.pasteInstanceAll();
                owner.clearFocus();
            };
        }
        syncTypeControl = std::make_unique<ChoiceControl>(owner.valueTreeState,
            owner.config.makeCrossoverParameterId("sync-type"), "NOTE SYNC");
        syncTypeControl->setBottomIndicatorsVisible(false, false);
        addAndMakeVisible(*syncTypeControl);
        bpmControl = std::make_unique<ParameterControl>(owner.valueTreeState,
            owner.config.makeCrossoverParameterId("bpm"), "MANUAL BPM", 2);
        bpmControl->setBottomIndicatorsVisible(false, false);
        addAndMakeVisible(*bpmControl);
        syncTypeControl->onValueChanged = [this] { refreshExternalState(); };
        refreshExternalState();
    }

    std::array<juce::Component*, 2> getMainSyncControls() const override
    {
        return {syncTypeControl.get(), bpmControl.get()};
    }

    std::vector<juce::Component*> getInstanceControls() const override
    {
        std::vector<juce::Component*> controls;
        // The INSTANCE page uses a single grid, not the old separate
        // CROSSOVER and INPUT rows. Keep their original reading order.
        for (const auto& group : controlGroups())
            for (const auto& row : group)
                for (const auto& cell : row)
                    if (cell.component != nullptr) controls.push_back(cell.component);
        return controls;
    }

    InstanceMainRows getInstanceMainRows() const override
    {
        InstanceMainRows rows;
        rows.secondary = {soloInstanceControl.get(), processingInputControl.get(), soloChannelControl.get()};
        rows.splits[0] = splitCountControl.get();
        for (size_t i = 0; i < splitControls.size(); ++i)
            rows.splits[i + 1] = splitControls[i].get();
        rows.actions = {autoSoloControl.get(), soloModeControl.get(),
                        copyAllControl.get(), pasteAllControl.get()};
        return rows;
    }

    int getPreferredHeight() const override
    {
        const auto width = juce::jmax(1, getWidth() - 2 * (controlBlockPadding + controlBlockFrameThickness));
        int height = controlBlockFrameOutset;
        bool first = true;
        for (const auto& group : controlGroups())
            for (const auto& row : group)
            {
                if (row.empty()) continue;
                if (!first) height += getUiRowGap(*this);
                height += measureControlGrid(*this, {0, 0, width, 0}, row).getHeight();
                first = false;
            }
        return height + controlBlockFrameOutset;
    }

    void refreshExternalState() override
    {
        if (!owner.config.showCrossoverControls) return;
        if (processingInputControl != nullptr && owner.config.isProcessingInputAvailable)
            if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(
                    owner.valueTreeState.getParameter(owner.config.processingInputParameterId)))
                for (int choice = 0; choice < parameter->choices.size(); ++choice)
                    processingInputControl->setChoiceEnabled(choice, owner.config.isProcessingInputAvailable(choice));
        if (autoSoloControl != nullptr)
        {
            const auto enabled = autoSoloControl->getValue() >= 0.5;
            if (owner.autoSoloEnabled != enabled) owner.setAutoSoloEnabled(enabled);
            autoSoloControl->setInteractionEnabled(owner.getActiveSplitCount() > 0);
        }
        bpmControl->setInteractionEnabled(syncTypeControl->getSelectedChoiceIndex() == 1);
        soloModeControl->setSelectedChoiceIndex(owner.manualSoloInclusive ? 1 : 0, false);
        soloModeControl->setInteractionEnabled(!owner.autoSoloEnabled && owner.getActiveSplitCount() > 0);
        for (size_t index = 0; index < splitControls.size(); ++index)
            splitControls[index]->setInteractionEnabled(index < owner.getActiveSplitCount());
    }

    void resized() override
    {
        const auto padding = controlBlockPadding + controlBlockFrameThickness;
        controlBlockFrames.clear();
        int rowY = controlBlockFrameOutset;
        bool first = true;
        for (const auto& group : controlGroups())
            for (const auto& row : group)
            {
                if (row.empty()) continue;
                if (!first) rowY += getUiRowGap(*this);
                for (const auto& cell : row)
                    if (cell.component != nullptr) cell.component->setVisible(true);
                const auto occupied = layoutControlGrid(*this,
                    {padding, rowY, getWidth() - 2 * padding, 0}, row);
                rowY = occupied.getBottom();
                first = false;
            }
    }

    void mouseDown(const juce::MouseEvent&) override { owner.clearFocus(); }

private:
    using ControlRow = std::vector<ControlGridCell>;
    using ControlGroup = std::vector<ControlRow>;

    std::array<ControlGroup, 2> controlGroups() const
    {
        std::array<ControlGroup, 2> groups;
        const auto add = [&groups](size_t group, size_t row, juce::Component* field)
        {
            if (field == nullptr) return;
            if (groups[group].size() <= row) groups[group].resize(row + 1);
            groups[group][row].push_back({field});
        };
        add(0, 0, splitCountControl.get());
        add(0, 0, autoSoloControl.get());
        add(0, 0, soloModeControl.get());
        // All five split frequencies stay on CROSSOVER's second row.
        for (const auto& control : splitControls) add(0, 1, control.get());
        add(1, 0, processingInputControl.get());
        add(1, 0, soloInstanceControl.get());
        add(1, 0, soloChannelControl.get());
        add(1, 0, copyAllControl.get());
        add(1, 0, pasteAllControl.get());
        return groups;
    }

    CrossoverModuleComponent& owner;
    std::unique_ptr<ChoiceControl> processingInputControl, syncTypeControl;
    std::unique_ptr<ParameterControl> bpmControl;
    std::unique_ptr<ParameterControl> splitCountControl, soloInstanceControl, soloChannelControl, autoSoloControl;
    std::unique_ptr<PushControl> copyAllControl, pasteAllControl;
    std::unique_ptr<LocalChoiceControl> soloModeControl;
    std::array<std::unique_ptr<ParameterControl>, splitControlCount> splitControls;
};

std::unique_ptr<CrossoverModulePage> makeCrossoverSettingsPage(CrossoverModuleComponent& owner)
{
    return std::make_unique<CrossoverSettingsPage>(owner);
}
