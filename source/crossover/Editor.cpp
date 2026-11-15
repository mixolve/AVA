#include "Editor.h"
#include "Page.h"
#include "RangePage.h"
#include "UiSupport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/EditorSetup.h"
#include "../shell/ParameterControl.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace crossover_ui;

CrossoverModuleComponent::CrossoverModuleComponent(Config configIn)
    : config(std::move(configIn)),
      valueTreeState(*config.valueTreeState)
{
    getProperties().set("emptyEditorBackground",true);
    jassert(config.valueTreeState != nullptr);
    jassert(config.rangeControls.empty()
            || (config.makeRangeParameterId != nullptr && config.getControlPolicy != nullptr));

    if (config.moduleKey.isNotEmpty())
        setName("module-" + config.moduleKey);

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
        instanceHeading = makeTextButton({});
        instanceHeading->setName("instance");
        instanceHeading->setSelectionHighlighted(false);
        instanceHeading->setInteractionOutlineEnabled(false);
        instanceHeading->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*instanceHeading);


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

        auto page = makeCrossoverRangePage(*this, rangeIndex);
        rangePages[rangeIndex] = std::move(page);
    }

    visibleRangeIndex = juce::jmin(visibleRangeIndex, getActiveRangeCount() - 1);

    if (config.showCrossoverNavigation)
        for (size_t range = 0; range < numRanges; ++range)
        {
            auto button = std::make_unique<PushControl>("BAND-" + juce::String(int(range + 1)), false);
            button->setRangeIndicatorsVisible(false);
            button->setBottomIndicatorsVisible(config.showCrossoverSolo, false);
            button->onTrigger = [this, range] { if (range < getActiveRangeCount()) selectCrossoverRange(range); };
            if (config.showCrossoverSolo)
            {
                const auto id = config.makeCrossoverSoloParameterId(range);
                button->bindParameterIndicators(&valueTreeState, id);
                auto indicator = std::make_unique<AccessoryButton>("SOLO");
                indicator->bindParameter(valueTreeState, id);
                indicator->onClick = [this, range] { toggleManualSolo(range); };
                button->setBottomAccessory(indicator.get());
                bandSoloIndicators[range] = std::move(indicator);
            }
            bandControls[range] = std::move(button);
        }

    crossoverSettingsPage = makeCrossoverSettingsPage(*this);
    updateSoloState();

    pageViewport.setInterceptsMouseClicks(false, true);
    pageViewport.setScrollBarsShown(false, false, true, false);
    pageViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    pageViewport.setWantsKeyboardFocus(false);
    pageViewport.setName("viewport");
    addAndMakeVisible(pageViewport);

    updateRangeNavigation();
    updatePageVisibility();
    observedState = valueTreeState.state;
    observedState.addListener(this);
    for (auto* parameter : valueTreeState.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter)) {
            observedParameterIds.push_back(ranged->getParameterID());
            valueTreeState.addParameterListener(ranged->getParameterID(), this);
        }
}

void CrossoverModuleComponent::setModuleDisplayTitle(const juce::String& title)
{
    if (getProperties().getWithDefault("moduleDisplayTitle", juce::String()).toString() == title)
        return;
    getProperties().set("moduleDisplayTitle", title);
    for (const auto& page : rangePages)
        if (page != nullptr)
        {
            for (auto& frame : page->controlBlockFrames)
                if (frame.title == "DYN") frame.displayTitle = title;
            page->repaint();
        }
}

void CrossoverModuleComponent::setInstanceName(const juce::String& name)
{
    if (instanceHeading != nullptr && instanceHeading->getButtonText() != name)
        instanceHeading->setButtonText(name);
}

std::vector<juce::Component*> CrossoverModuleComponent::getMainControls()
{
    getProperties().set("sharedMainControls", true);
    pageViewport.setViewedComponent(nullptr, false);

    std::vector<juce::Component*> controls;
    // The instance name is rendered on the outer INSTANCE header instead.
    if (instanceHeading != nullptr) instanceHeading->setVisible(false);

    // Every BAND has a permanent slot in MAIN. XOV only enables/disables
    // navigation; it no longer changes the number of controls or their rows.
    for (const auto& band : bandControls)
        if (band != nullptr) controls.push_back(band.get());
    return controls;
}

std::array<juce::Component*, 2> CrossoverModuleComponent::getMainSyncControls() const
{
    return crossoverSettingsPage != nullptr ? crossoverSettingsPage->getMainSyncControls()
                                            : std::array<juce::Component*, 2> {};
}

std::vector<juce::Component*> CrossoverModuleComponent::getInstanceSettingsControls() const
{
    return crossoverSettingsPage != nullptr && config.showCrossoverControls
        ? crossoverSettingsPage->getInstanceControls()
        : std::vector<juce::Component*>{};
}

InstanceMainRows CrossoverModuleComponent::getInstanceMainRows() const
{
    return crossoverSettingsPage != nullptr && config.showCrossoverControls
        ? crossoverSettingsPage->getInstanceMainRows() : InstanceMainRows{};
}

CrossoverModulePage* CrossoverModuleComponent::sharedSettingsPage(juce::Component& parent, int width)
{
    auto* page = crossoverSettingsPage.get();
    if (page == nullptr) return nullptr;
    if (page->getParentComponent() != &parent) parent.addChildComponent(*page);
    page->setSize(width, page->getHeight());
    page->setSize(width, page->getPreferredHeight());
    page->setVisible(config.showCrossoverControls);
    return page;
}

void CrossoverModuleComponent::updateSoloState()
{
    for (size_t range = 0; range < numRanges; ++range)
        if (bandSoloIndicators[range] != nullptr)
            bandSoloIndicators[range]->setEnabled(!autoSoloEnabled && getActiveRangeCount() > 1 && range < getActiveRangeCount());
}

CrossoverModuleComponent::~CrossoverModuleComponent()
{
    observedState.removeListener(this);
    for (const auto& id : observedParameterIds) valueTreeState.removeParameterListener(id, this);
    saveUiState();
    pageViewport.setViewedComponent(nullptr, false);
}



std::vector<std::pair<int, juce::Component*>> CrossoverModuleComponent::independentBlocks(juce::Component& parent, int width)
{
    getProperties().set("flatBlocks", true);
    pageViewport.setViewedComponent(nullptr, false);
    for (auto& page : rangePages)
        if (auto* range = dynamic_cast<CrossoverRangePage*>(page.get())) range->hideIndependentBlocks();
    if (auto* range = dynamic_cast<CrossoverRangePage*>(getCurrentPageComponent()))
        return range->independentBlocks(parent, width);
    return {};
}
