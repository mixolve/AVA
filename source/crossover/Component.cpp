#include "Component.h"
#include "Page.h"
#include "UiSupport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/LocalParameterControl.h"
#include "../shell/ParameterControl.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace crossover_ui;

CrossoverModuleComponent::CrossoverModuleComponent(Config configIn)
    : config(std::move(configIn)),
      valueTreeState(*config.valueTreeState)
{
    jassert(config.valueTreeState != nullptr);
    jassert((config.rangeControls.empty() && config.rangeTailControls.empty())
            || config.makeRangeParameterId != nullptr);

    const auto requiresCrossoverState = config.showCrossoverControls || config.showCrossoverNavigation || config.showCrossoverSolo;

    if (requiresCrossoverState)
    {
        jassert(config.makeCrossoverParameterId != nullptr);
        jassert(config.makeCrossoverSoloParameterId != nullptr);
        jassert(config.makeCrossoverSplitCountParameterId != nullptr);

        activeSplitCountParameter = dynamic_cast<juce::RangedAudioParameter*>(
            valueTreeState.getParameter(config.makeCrossoverSplitCountParameterId()));
        jassert(activeSplitCountParameter != nullptr);
    }

    loadUiState();

    if (config.showCrossoverSolo)
    {
        instanceHeading = makeTextButton({}, uiGrey500);
        instanceHeading->setFillVisible(false);
        instanceHeading->setAlwaysAccentOutline(false);
        instanceHeading->setToggleAccentVisible(false);
        instanceHeading->setPressFillEnabled(false);
        instanceHeading->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*instanceHeading);

        headerSoloButton = makeTextButton("SOLO");
        headerSoloButton->setClickingTogglesState(false);
        headerSoloButton->setLongPressPromptActions({}, [this]
        {
            if (config.makeCrossoverSoloParameterId != nullptr)
                assignButtonToHostSlot(config.makeCrossoverSoloParameterId(visibleRangeIndex),
                                       "SOLO", headerSoloButton.get());
        });
        headerSoloButton->onClick = [this]
        {
            toggleManualSolo(visibleRangeIndex);
            updateHeaderSoloState();
        };
        addAndMakeVisible(*headerSoloButton);
    }

    size_t activeSoloCount = 0;

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto* parameter = requiresCrossoverState
            ? dynamic_cast<juce::RangedAudioParameter*>(valueTreeState.getParameter(config.makeCrossoverSoloParameterId(rangeIndex)))
            : nullptr;
        jassert(! requiresCrossoverState || parameter != nullptr);
        soloParameters[rangeIndex] = parameter;

        if (parameter != nullptr && parameter->getValue() >= 0.5f)
        {
            if (! uiStateLoaded && activeSoloCount == 0)
                visibleRangeIndex = rangeIndex;

            ++activeSoloCount;

            if (! uiStateLoaded && activeSoloCount == 1)
                manualSoloMask[rangeIndex] = true;
        }

        if (config.showCrossoverNavigation)
        {
            auto button = makeTextButton(juce::String(static_cast<int>(rangeIndex + 1)));
            button->setClickingTogglesState(false);
            button->onClick = [this, rangeIndex] { selectCrossoverRange(rangeIndex); };
            addAndMakeVisible(*button);
            monitorButtons[rangeIndex] = std::move(button);
        }

        auto page = makeCrossoverRangePage(*this, rangeIndex, uiAccent);
        rangePages[rangeIndex] = std::move(page);
    }

    if (! uiStateLoaded)
        crossoverSettingsActive = config.startOnCrossoverSettings;

    visibleRangeIndex = juce::jmin(visibleRangeIndex, getActiveRangeCount() - 1);

    if (config.showCrossoverNavigation)
    {
        auto allButton = makeTextButton({});
        allButton->setTablerIcon("adjustments-alt");
        allButton->setClickingTogglesState(false);
        allButton->onClick = [this] { showCrossoverSettings(); };
        addAndMakeVisible(*allButton);
        monitorButtons[numRanges] = std::move(allButton);
    }

    crossoverSettingsPage = makeCrossoverSettingsPage(*this);
    updateHeaderSoloState();

    pageViewport.setInterceptsMouseClicks(false, true);
    pageViewport.setScrollBarsShown(false, false);
    pageViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    pageViewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(pageViewport);

    updateMonitorButtons();
    updatePageVisibility();
}

void CrossoverModuleComponent::setInstanceName(const juce::String& name)
{
    if (instanceHeading != nullptr && instanceHeading->getButtonText() != name)
        instanceHeading->setButtonText(name);
}

void CrossoverModuleComponent::setModuleActionButtons(BoxTextButton& addButton,
                                                       BoxTextButton& titleButton)
{
    moduleAddButton = &addButton;
    moduleTitleButton = &titleButton;
    if (addButton.getParentComponent() != this)
        addChildComponent(addButton);
    if (titleButton.getParentComponent() != this)
        addChildComponent(titleButton);
    resized();
}

void CrossoverModuleComponent::updateHeaderSoloState()
{
    if (headerSoloButton == nullptr)
        return;

    const auto enabled = ! crossoverSettingsActive && ! autoSoloEnabled && getActiveRangeCount() > 1;
    headerSoloButton->setVisible(! crossoverSettingsActive);
    headerSoloButton->setEnabled(enabled);
    headerSoloButton->setAlpha(1.0f);
    headerSoloButton->setToggleState(enabled && isRangeSoloEnabled(visibleRangeIndex), juce::dontSendNotification);
    headerSoloButton->getProperties().set(juce::Identifier("oscParameterId"),
                                           config.makeCrossoverSoloParameterId(visibleRangeIndex));
}

CrossoverModuleComponent::~CrossoverModuleComponent()
{
    saveUiState();
    if (pinnedHeaderComponent != nullptr)
        removeChildComponent(pinnedHeaderComponent);
    if (pinnedTailComponent != nullptr)
        removeChildComponent(pinnedTailComponent);
    pageViewport.setViewedComponent(nullptr, false);
}
