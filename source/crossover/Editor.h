#pragma once
#include "../shell/Controls.h"

#include "../modules/shared/BlockCatalog.h"
#include "../modules/gain/Order.h"

#include "Splitter.h"
#include "Page.h"

#include <JuceHeader.h>
#include "../modules/shared/ParameterLayout.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

class BoxTextButton;
class ChoiceControl;
class LocalChoiceControl;
class PushControl;
class ParameterControl;
class AccessoryButton;
class CrossoverModulePage;
class CrossoverRangePage;
class CrossoverSettingsPage;

class CrossoverModuleComponent final : public juce::Component, private juce::AudioProcessorValueTreeState::Listener, private juce::ValueTree::Listener
{
public:
    enum class ControlKind
    {
        parameter,
        choice,
        toggle,
        readout,
        custom,
        time
    };

    struct CrossoverControlSpec
    {
        ControlKind kind = ControlKind::parameter;
        const char* suffix = "";
        const char* label = "";
        int decimals = 2;
        const char* modeSuffix = "";
        bool hasTopGap = true;
        int sourceRangeIndex = -1;
        int controlsInRow = 1;
        bool startNewControlRow = false;
    };

    struct Config
    {
        void* processorIdentity = nullptr;
        juce::String moduleKey;
        // Unique, persistent identity for UI collapse state. Copied modules
        // must not share the original module's collapsed.* properties.
        juce::String collapseScope;
        juce::AudioProcessorValueTreeState* valueTreeState = nullptr;
        // UI-only collapse metadata is saved by the owning instance, never the
        // transient per-module DSP state.
        juce::AudioProcessorValueTreeState* uiState = nullptr;
        std::function<void()> markParametersDirty;
        std::function<bool()> copyInstanceAll;
        std::function<bool()> pasteInstanceAll;
        std::function<juce::String(size_t, const char*)> makeRangeParameterId;
        std::function<juce::String(const char*)> makeCrossoverParameterId;
        std::function<juce::String(size_t)> makeCrossoverSoloParameterId;
        std::function<juce::String()> makeCrossoverSplitCountParameterId;
        juce::String processingInputParameterId;
        juce::String outputMuteParameterId;
        juce::String soloChannelParameterId;
        std::function<bool(int)> isProcessingInputAvailable;
        std::function<bool(const juce::String&, int)> isRangeChoiceAvailable;
        std::function<ava::modules::chain::gain::Order(size_t)> getGainOrder;
        std::vector<CrossoverControlSpec> rangeControls;
        std::function<std::unique_ptr<juce::Component>(size_t,const juce::String&)> makeCustomControl;
        std::function<ava::modules::parameters::ControlPolicy(size_t, const juce::String&)> getControlPolicy;
        int crossoverDecimals = 0;
        bool showCrossoverControls = true;
        bool showCrossoverNavigation = true;
        bool showCrossoverSolo = true;
        std::function<void()> onPageChanged;
        std::function<juce::StringArray()> getNoteChoices;
        std::function<double()> getTempoBpm;

        std::function<void()> clearKeyboardFocus;
    };

    explicit CrossoverModuleComponent(Config configIn);
    ~CrossoverModuleComponent() override;

    void* getProcessorIdentity() const noexcept { return config.processorIdentity; }

    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void refreshExternalState();
    void refreshRangeControls();
    void setInstanceName(const juce::String& name);
    void configureModuleScrollTile(const juce::String& title, const std::function<bool()>& isBypassed,
                                   const std::function<void()>& toggle,
                                   const std::function<void()>& copy,
                                   const std::function<void()>& paste);
    std::vector<juce::Component*> getMainControls();
    std::array<juce::Component*, 2> getMainSyncControls() const;
    std::vector<juce::Component*> getInstanceSettingsControls() const;
    InstanceMainRows getInstanceMainRows() const;
    CrossoverModulePage* sharedSettingsPage(juce::Component& parent, int width);
    std::vector<std::pair<int, juce::Component*>> independentBlocks(juce::Component& parent, int width);
    int getPreferredHeight() const noexcept;
    size_t getVisibleCrossoverRange() const noexcept { return visibleRangeIndex; }
    void setExternalCrossoverRange(size_t rangeIndex);
    void selectCrossoverRange(size_t rangeIndex);

private:
    static constexpr size_t numRanges = ava::crossover::Splitter::numRanges;

    void loadUiState();
    void saveUiState();
    bool restoreUiStateIfChanged();
    juce::String getUiStateSignature() const;
    void toggleManualSolo(size_t rangeIndex);
    void setAutoSoloEnabled(bool shouldBeEnabled);
    void setManualSoloInclusive(bool shouldBeInclusive);
    void activeSplitCountChanged();
    void syncMonitorParameters();
    void synchroniseManualSoloMaskFromParameters();
    bool isRangeSoloEnabled(size_t rangeIndex) const noexcept;
    void updateRangeNavigation();
    void updateSoloState();
    void updatePageVisibility();
    void updatePageViewport();
    CrossoverModulePage* getCurrentPageComponent() const noexcept;
    int getCurrentPagePreferredHeight() const noexcept;
    size_t getActiveSplitCount() const;
    size_t getActiveRangeCount() const;
    bool setParameterNormalisedValue(juce::RangedAudioParameter& parameter, float normalisedValue);
    void clearFocus();
    void notifyPageChanged();

    void parameterChanged(const juce::String& parameterId, float) override
    {
        // Module parameters are not all layout dependencies. A knob gesture
        // must not schedule a full module refresh on every value notification.
        if ((config.moduleKey == "chain" || config.moduleKey == "dyn")
            && !uiDependentParameterIds.contains(parameterId)
            && !parameterId.endsWith(".hidden"))
            return;
        externalStateDirty.store(true, std::memory_order_release);
    }
    void valueTreePropertyChanged(juce::ValueTree& changed, const juce::Identifier&) override
    {
        // APVTS mirrors numeric changes into PARAM nodes. They are not
        // structural UI changes and must not reflow MODULES during a drag.
        if (changed.hasType("PARAM")) return;
        if (!writingUiState.load()) externalStateDirty.store(true);
    }
    void valueTreeRedirected(juce::ValueTree&) override { externalStateDirty.store(true); }
    std::atomic<bool> externalStateDirty { true }, writingUiState { false };
    juce::ValueTree observedState;
    std::vector<juce::String> observedParameterIds;
    juce::StringArray uiDependentParameterIds;
    uint32_t lastAvailabilityRefresh = 0;
    Config config;
    juce::AudioProcessorValueTreeState& valueTreeState;
    std::array<std::unique_ptr<PushControl>, numRanges> bandControls;
    std::array<std::unique_ptr<AccessoryButton>, numRanges> bandSoloIndicators;
    std::unique_ptr<BoxTextButton> instanceHeading;
    std::array<juce::RangedAudioParameter*, numRanges> soloParameters {};
    juce::RangedAudioParameter* activeSplitCountParameter = nullptr;
    std::array<std::unique_ptr<CrossoverModulePage>, numRanges> rangePages;
    std::unique_ptr<CrossoverModulePage> crossoverSettingsPage;
    UiScrollViewport pageViewport;
    bool uiStateLoaded = false;
    bool pageScrollRestored = false;
    size_t visibleRangeIndex = 0;
    int restoredPageScrollY = 0;
    juce::String uiStateSignature;
    std::array<bool, numRanges> manualSoloMask {};
    bool autoSoloEnabled = false;
    bool manualSoloInclusive = false;

    friend class CrossoverRangePage;
    friend class CrossoverSettingsPage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrossoverModuleComponent)
};
