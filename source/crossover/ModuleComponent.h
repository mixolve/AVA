#pragma once

#include "Splitter.h"

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

class BoxTextButton;
class ChoiceControl;
class ParameterControl;
class CrossoverModulePage;
class CrossoverRangePage;
class CrossoverSettingsPage;

class CrossoverModuleComponent final : public juce::Component
{
public:
    enum class ControlKind
    {
        heading,
        parameter,
        choice,
        toggle,
        inactive,
        readout,
        time
    };

    struct CrossoverControlSpec
    {
        ControlKind kind = ControlKind::parameter;
        const char* suffix = "";
        const char* label = "";
        int decimals = 2;
        const char* enabledLabel = "";
        const char* disabledLabel = "";
        const char* modeSuffix = "";
        const char* syncSuffix = "";
        const char* exclusiveGroup = "";
        const char* auxiliaryToggleSuffix = "";
        const char* auxiliaryToggleLabel = "";
        const char* auxiliaryToggleSymbol = "";
        const char* enabledWhenSuffix = "";
        const char* reorderGroup = "";
        const char* orderSuffix = "";
        bool fixedOrder = false;
        bool toggleAccentVisible = true;
        bool auxiliaryToggleInverted = false;
        bool showTimeModeButton = true;
        int topGapMultiplier = 1;
        int parameterTitleWidth = 0;
        int auxiliaryToggleWidth = 0;
        int sourceRangeIndex = -1;
        int controlsInRow = 1;
    };

    struct Config
    {
        void* processorIdentity = nullptr;
        juce::String moduleKey;
        juce::AudioProcessorValueTreeState* valueTreeState = nullptr;
        juce::UndoManager* undoManager = nullptr;
        std::function<void()> markParametersDirty;
        std::function<bool()> refreshExternalState;
        std::function<bool(const juce::String&, const juce::String&, float)> assignHostSlot;
        std::function<juce::String(size_t, const char*)> makeRangeParameterId;
        std::function<juce::String(const char*)> makeCrossoverParameterId;
        std::function<juce::String(size_t)> makeCrossoverSoloParameterId;
        std::function<juce::String()> makeCrossoverSplitCountParameterId;
        std::vector<CrossoverControlSpec> rangeControls;
        std::vector<CrossoverControlSpec> rangeTailControls;
        int crossoverDecimals = 0;
        bool showAutoSolo = true;
        bool startOnCrossoverSettings = false;
        bool showCrossoverControls = true;
        bool showModuleHeading = true;
        bool pinModuleHeading = false;
        bool showCrossoverNavigation = true;
        bool showCrossoverSolo = true;
        juce::String crossoverSettingsHeading;
        std::function<void()> onPageChanged;
        std::function<void()> onModuleCloseRequest;
        std::function<juce::StringArray()> getHostSyncChoices;
        std::function<int()> getDefaultHostSyncChoiceIndex;
        std::function<void(const juce::Rectangle<int>&,
                           const juce::StringArray&,
                           int,
                           std::vector<bool>,
                           juce::Justification,
                           std::function<void(int)>,
                           std::function<void()>,
                           std::function<void()>)> showChoicePrompt;
        std::function<void()> clearKeyboardFocus;
    };

    explicit CrossoverModuleComponent(Config configIn);
    ~CrossoverModuleComponent() override;

    void* getProcessorIdentity() const noexcept { return config.processorIdentity; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    juce::Rectangle<int> getContentBounds() const noexcept;
    void refreshCurrentPageLayout();
    void refreshExternalState();
    int getPreferredHeight() const noexcept;
    bool isCrossoverSettingsSelected() const noexcept { return crossoverSettingsActive; }
    size_t getVisibleCrossoverRange() const noexcept { return visibleRangeIndex; }
    void setExternalCrossoverRange(size_t rangeIndex);

private:
    static constexpr size_t numRanges = ava::crossover::Splitter::numRanges;
    static constexpr size_t numMonitorButtons = numRanges + 1;

    void loadUiState();
    void saveUiState();
    bool restoreUiStateIfChanged();
    juce::String getUiStateSignature() const;
    void selectCrossoverRange(size_t rangeIndex);
    void toggleManualSolo(size_t rangeIndex);
    void showCrossoverSettings();
    void setAutoSoloEnabled(bool shouldBeEnabled);
    void setManualSoloInclusive(bool shouldBeInclusive);
    void changeActiveSplitCount(int delta);
    void syncMonitorParameters();
    void synchroniseManualSoloMaskFromParameters();
    bool isRangeSoloEnabled(size_t rangeIndex) const noexcept;
    void updateMonitorButtons();
    void updatePageVisibility();
    void updatePinnedHeaderComponent();
    void updatePinnedTailComponent();
    void scrollPageViewport(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel);
    void updatePageViewport();
    CrossoverModulePage* getCurrentPageComponent() const noexcept;
    int getCurrentPagePreferredHeight() const noexcept;
    int getCurrentPinnedHeaderHeight() const noexcept;
    int getCurrentPinnedTailHeight() const noexcept;
    size_t getActiveSplitCount() const;
    size_t getActiveRangeCount() const;
    bool constrainCrossoverFrequency(size_t crossoverIndex);
    bool setParameterPlainValue(const juce::String& parameterId, float plainValue);
    bool swapParameterPlainValues(const juce::String& firstParameterId,
                                  const juce::String& secondParameterId);
    bool setParameterNormalisedValue(juce::RangedAudioParameter& parameter, float normalisedValue);
    bool assignButtonToHostSlot(const juce::String& parameterId,
                                const juce::String& fallbackName,
                                const BoxTextButton* button);
    void clearFocus();
    void notifyPageChanged();

    Config config;
    juce::AudioProcessorValueTreeState& valueTreeState;
    std::array<std::unique_ptr<BoxTextButton>, numMonitorButtons> monitorButtons;
    std::array<juce::RangedAudioParameter*, numRanges> soloParameters {};
    juce::RangedAudioParameter* activeSplitCountParameter = nullptr;
    std::array<std::unique_ptr<CrossoverModulePage>, numRanges> rangePages;
    std::unique_ptr<CrossoverModulePage> crossoverSettingsPage;
    juce::Viewport pageViewport;
    juce::Component* pinnedHeaderComponent = nullptr;
    juce::Component* pinnedTailComponent = nullptr;
    bool uiStateLoaded = false;
    bool pageScrollRestored = false;
    size_t visibleRangeIndex = 0;
    int restoredPageScrollY = 0;
    juce::String uiStateSignature;
    std::array<bool, numRanges> manualSoloMask {};
    bool crossoverSettingsActive = false;
    bool autoSoloEnabled = false;
    bool manualSoloInclusive = false;

    friend class CrossoverRangePage;
    friend class CrossoverSettingsPage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrossoverModuleComponent)
};
