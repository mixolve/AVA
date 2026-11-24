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
    // MODULES' thumbs belong to their scrolling controls rather than the
    // outer editor overlay; otherwise the two repaint paths can show different frames.
    getProperties().set("paintRangeLocally", config.moduleKey == "chain" || config.moduleKey == "dyn");

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
    // Capture the dependencies declared by the control policies. Only these
    // parameters can affect the module control layout/enablement; ordinary
    // value movement is already handled by each control's APVTS attachment.
    if ((config.moduleKey == "chain" || config.moduleKey == "dyn")
        && config.getControlPolicy && config.makeRangeParameterId)
    {
        for (size_t range = 0; range < numRanges; ++range)
            for (const auto& spec : config.rangeControls)
            {
                const auto policy = config.getControlPolicy(range, spec.suffix);
                for (const auto* suffix : policy.dependencies)
                    uiDependentParameterIds.addIfNotAlreadyThere(config.makeRangeParameterId(range, suffix));
                if (spec.kind == ControlKind::readout)
                {
                    uiDependentParameterIds.addIfNotAlreadyThere(config.makeRangeParameterId(range, spec.suffix));
                    if (spec.modeSuffix != nullptr && spec.modeSuffix[0] != '\0')
                        uiDependentParameterIds.addIfNotAlreadyThere(config.makeRangeParameterId(range, spec.modeSuffix));
                }
            }
    }

    observedState = valueTreeState.state;
    observedState.addListener(this);
    for (auto* parameter : valueTreeState.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter)) {
            const auto id = ranged->getParameterID();
            if ((config.moduleKey == "chain" || config.moduleKey == "dyn")
                && !uiDependentParameterIds.contains(id) && !id.endsWith(".hidden"))
                continue;
            observedParameterIds.push_back(id);
            valueTreeState.addParameterListener(id, this);
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

void CrossoverModuleComponent::setModuleNameplateBypass(std::function<bool()> isBypassed,
                                                       std::function<void()> toggle)
{
    for (auto& page : rangePages)
        if (page != nullptr)
            page->setModuleNameplateBypass(isBypassed, toggle);
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
    if (instanceHeading != nullptr) instanceHeading->setVisible(false);

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

using namespace crossover_ui;

size_t CrossoverModuleComponent::getActiveSplitCount() const
{
    if (activeSplitCountParameter == nullptr)
        return numRanges - 1;

    return static_cast<size_t>(juce::jlimit(0,
                                           static_cast<int>(numRanges - 1),
                                           static_cast<int>(std::round(activeSplitCountParameter->convertFrom0to1(
                                               activeSplitCountParameter->getValue())))));
}

size_t CrossoverModuleComponent::getActiveRangeCount() const
{
    return getActiveSplitCount() + 1;
}

bool CrossoverModuleComponent::setParameterNormalisedValue(juce::RangedAudioParameter& parameter, const float normalisedValue)
{
    const auto value = juce::jlimit(0.0f, 1.0f, normalisedValue);
    const auto wasDifferent = std::abs(parameter.getValue() - value) > 1.0e-6f;

    if (! wasDifferent)
        return false;

    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(value);
    parameter.endChangeGesture();

    if (config.markParametersDirty != nullptr)
        config.markParametersDirty();

    return true;
}

void CrossoverModuleComponent::clearFocus()
{
    if (config.clearKeyboardFocus != nullptr)
        config.clearKeyboardFocus();
    else
        clearKeyboardFocus(*this);
}
