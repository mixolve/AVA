#include "ModuleComponent.h"
#include "Pages.h"
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
        allButton->setSystemSymbol("gearshape");
        allButton->setClickingTogglesState(false);
        allButton->onClick = [this] { showCrossoverSettings(); };
        addAndMakeVisible(*allButton);
        monitorButtons[numRanges] = std::move(allButton);
    }

    crossoverSettingsPage = makeCrossoverSettingsPage(*this);

    pageViewport.setInterceptsMouseClicks(false, true);
    pageViewport.setScrollBarsShown(false, false);
    pageViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
    pageViewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(pageViewport);

    updateMonitorButtons();
    updatePageVisibility();
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

void CrossoverModuleComponent::loadUiState()
{
    auto& state = valueTreeState.state;
    autoSoloEnabled = getBool(state, config.moduleKey, "autoSoloEnabled", false);
    manualSoloInclusive = getBool(state, config.moduleKey, "manualSoloInclusive", false);
    crossoverSettingsActive = getBool(state, config.moduleKey, "crossoverSettingsActive", false);
    visibleRangeIndex = static_cast<size_t>(juce::jlimit(0,
                                                       static_cast<int>(numRanges - 1),
                                                       getInt(state, config.moduleKey, "visibleRangeIndex", 0)));
    restoredPageScrollY = juce::jmax(0, getInt(state, config.moduleKey, "pageScrollY", 0));
    pageScrollRestored = false;

    manualSoloMask = {};

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        manualSoloMask[rangeIndex] = getBool(state,
                                            config.moduleKey,
                                            "manualSolo." + juce::String(static_cast<int>(rangeIndex)),
                                            false);
    }

    uiStateLoaded = getBool(state, config.moduleKey, "hasUiState", false);
    uiStateSignature = getUiStateSignature();
}

void CrossoverModuleComponent::saveUiState()
{
    auto& state = valueTreeState.state;

    if (pageScrollRestored)
        restoredPageScrollY = pageViewport.getViewPositionY();
    setBool(state, config.moduleKey, "autoSoloEnabled", autoSoloEnabled);
    setBool(state, config.moduleKey, "manualSoloInclusive", manualSoloInclusive);
    setBool(state, config.moduleKey, "crossoverSettingsActive", crossoverSettingsActive);
    setInt(state, config.moduleKey, "visibleRangeIndex", static_cast<int>(visibleRangeIndex));
    setInt(state, config.moduleKey, "pageScrollY", restoredPageScrollY);
    setBool(state, config.moduleKey, "hasUiState", true);

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        setBool(state,
                config.moduleKey,
                "manualSolo." + juce::String(static_cast<int>(rangeIndex)),
                manualSoloMask[rangeIndex]);

    uiStateSignature = getUiStateSignature();
}

bool CrossoverModuleComponent::restoreUiStateIfChanged()
{
    if (getUiStateSignature() == uiStateSignature)
        return false;

    loadUiState();
    visibleRangeIndex = juce::jmin(visibleRangeIndex, getActiveRangeCount() - 1);
    updateMonitorButtons();
    updatePageVisibility();
    return true;
}

juce::String CrossoverModuleComponent::getUiStateSignature() const
{
    const auto& state = valueTreeState.state;
    juce::StringArray values;
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "hasUiState"), false).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "autoSoloEnabled"), false).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "manualSoloInclusive"), false).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "crossoverSettingsActive"), false).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "visibleRangeIndex"), 0).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "pageScrollY"), 0).toString());

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        values.add(state.getProperty(makeStatePropertyId(config.moduleKey,
                                                          "manualSolo." + juce::String(static_cast<int>(rangeIndex))),
                                      false).toString());
    }

    return values.joinIntoString("|");
}

void CrossoverModuleComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
}

void CrossoverModuleComponent::resized()
{
    auto bounds = getContentBounds();
    if (config.showCrossoverNavigation)
    {
        auto monitorRow = bounds.removeFromTop(rowHeight);
        const auto buttonCount = static_cast<int>(monitorButtons.size());
        const auto totalGapWidth = parameterGap * (buttonCount - 1);
        const auto baseButtonWidth = (monitorRow.getWidth() - totalGapWidth) / buttonCount;
        auto remainder = (monitorRow.getWidth() - totalGapWidth) - (baseButtonWidth * buttonCount);

        for (auto& button : monitorButtons)
        {
            const auto buttonWidth = baseButtonWidth + (remainder > 0 ? 1 : 0);

            if (button != nullptr)
                button->setBounds(monitorRow.removeFromLeft(buttonWidth));
            else
                monitorRow.removeFromLeft(buttonWidth);

            monitorRow.removeFromLeft(parameterGap);
            remainder = juce::jmax(0, remainder - 1);
        }

        if (! bounds.isEmpty())
            bounds.removeFromTop(verticalGap);
    }

    updatePinnedHeaderComponent();
    const auto pinnedHeaderHeight = getCurrentPinnedHeaderHeight();
    auto pinnedHeaderBounds = bounds.removeFromTop(pinnedHeaderHeight);

    if (pinnedHeaderComponent != nullptr)
        pinnedHeaderComponent->setBounds(pinnedHeaderBounds);

    if (pinnedHeaderHeight > 0 && ! bounds.isEmpty())
        bounds.removeFromTop(verticalGap);

    updatePinnedTailComponent();
    const auto pinnedTailHeight = getCurrentPinnedTailHeight();
    auto pinnedTailBounds = bounds.removeFromBottom(pinnedTailHeight);
    pageViewport.setBounds(bounds);

    if (pinnedTailComponent != nullptr)
    {
        pinnedTailComponent->setBounds(pinnedTailBounds);
        if (auto* currentPage = getCurrentPageComponent())
            currentPage->layoutPinnedTail();
    }
    updatePageViewport();
}

void CrossoverModuleComponent::mouseDown(const juce::MouseEvent&)
{
    clearFocus();
}

void CrossoverModuleComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    scrollPageViewport(event, wheel);
}

void CrossoverModuleComponent::scrollPageViewport(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (pageViewport.getViewedComponent() == nullptr)
        return;

    const auto editorPosition = event.getEventRelativeTo(this).getPosition();

    if (! pageViewport.getBounds().contains(editorPosition))
        return;

    scrollViewportWithWheel(pageViewport,
                            pageViewport.getViewedComponent()->getHeight(),
                            wheel,
                            event.mods.isShiftDown());
}

juce::Rectangle<int> CrossoverModuleComponent::getContentBounds() const noexcept
{
    return getLocalBounds();
}

void CrossoverModuleComponent::refreshCurrentPageLayout()
{
    updatePageViewport();
}

void CrossoverModuleComponent::refreshExternalState()
{
    if (! restoreUiStateIfChanged()
        && pageScrollRestored
        && pageViewport.getViewPositionY() != restoredPageScrollY)
        saveUiState();

    if (config.refreshExternalState != nullptr && ! config.refreshExternalState())
        return;

    synchroniseManualSoloMaskFromParameters();

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateMonitorButtons();
    refreshCurrentPageLayout();
}

int CrossoverModuleComponent::getPreferredHeight() const noexcept
{
    const auto pinnedHeaderHeight = getCurrentPinnedHeaderHeight();

    return (config.showCrossoverNavigation ? rowHeight + verticalGap : 0)
        + (pinnedHeaderHeight > 0 ? pinnedHeaderHeight + verticalGap : 0)
        + getCurrentPagePreferredHeight()
        + getCurrentPinnedTailHeight();
}

void CrossoverModuleComponent::setExternalCrossoverRange(const size_t rangeIndex)
{
    crossoverSettingsActive = false;
    visibleRangeIndex = juce::jmin(rangeIndex, getActiveRangeCount() - 1);
    updatePageVisibility();
    resized();
}

void CrossoverModuleComponent::selectCrossoverRange(const size_t rangeIndex)
{
    visibleRangeIndex = juce::jmin(rangeIndex, getActiveRangeCount() - 1);
    crossoverSettingsActive = false;

    if (autoSoloEnabled)
        manualSoloMask = {};

    updateMonitorButtons();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    resized();
    notifyPageChanged();
    clearFocus();
}

void CrossoverModuleComponent::toggleManualSolo(const size_t rangeIndex)
{
    if (autoSoloEnabled || getActiveRangeCount() <= 1 || rangeIndex >= getActiveRangeCount())
        return;

    visibleRangeIndex = juce::jmin(rangeIndex, numRanges - 1);
    crossoverSettingsActive = false;
    const auto shouldSoloRange = ! manualSoloMask[visibleRangeIndex];

    if (! manualSoloInclusive)
        manualSoloMask = {};

    manualSoloMask[visibleRangeIndex] = shouldSoloRange;
    updateMonitorButtons();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    clearFocus();
}

void CrossoverModuleComponent::changeActiveSplitCount(const int delta)
{
    if (activeSplitCountParameter == nullptr)
        return;

    const auto currentValue = static_cast<int>(
        std::round(activeSplitCountParameter->convertFrom0to1(activeSplitCountParameter->getValue())));
    const auto newValue = juce::jlimit(0, static_cast<int>(numRanges - 1), currentValue + delta);

    if (newValue == currentValue)
        return;

    setParameterPlainValue(config.makeCrossoverSplitCountParameterId(), static_cast<float>(newValue));
    visibleRangeIndex = juce::jmin(visibleRangeIndex, getActiveRangeCount() - 1);
    manualSoloMask = {};

    for (size_t crossoverIndex = 0; crossoverIndex < static_cast<size_t>(newValue); ++crossoverIndex)
        constrainCrossoverFrequency(crossoverIndex);

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateMonitorButtons();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    resized();
}

void CrossoverModuleComponent::showCrossoverSettings()
{
    crossoverSettingsActive = true;
    updateMonitorButtons();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    resized();
    notifyPageChanged();
    clearFocus();
}

void CrossoverModuleComponent::notifyPageChanged()
{
    if (config.onPageChanged != nullptr)
        config.onPageChanged();
}

void CrossoverModuleComponent::setAutoSoloEnabled(const bool shouldBeEnabled)
{
    autoSoloEnabled = shouldBeEnabled;
    manualSoloMask = {};

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateMonitorButtons();
    syncMonitorParameters();
    saveUiState();
}

void CrossoverModuleComponent::setManualSoloInclusive(const bool shouldBeInclusive)
{
    manualSoloInclusive = shouldBeInclusive;

    if (! manualSoloInclusive)
    {
        size_t soloRangeToKeep = visibleRangeIndex;

        if (! manualSoloMask[soloRangeToKeep])
        {
            for (size_t rangeIndex = 0; rangeIndex < getActiveRangeCount(); ++rangeIndex)
            {
                if (manualSoloMask[rangeIndex])
                {
                    soloRangeToKeep = rangeIndex;
                    break;
                }
            }
        }

        const auto shouldKeepSolo = soloRangeToKeep < getActiveRangeCount() && manualSoloMask[soloRangeToKeep];
        manualSoloMask = {};

        if (shouldKeepSolo)
            manualSoloMask[soloRangeToKeep] = true;
    }

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateMonitorButtons();
    syncMonitorParameters();
    saveUiState();
}

void CrossoverModuleComponent::syncMonitorParameters()
{
    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto* parameter = soloParameters[rangeIndex];

        if (parameter == nullptr)
            continue;

        const auto enabled = rangeIndex < getActiveRangeCount()
            && ((autoSoloEnabled && ! crossoverSettingsActive && rangeIndex == visibleRangeIndex)
                || (! autoSoloEnabled && manualSoloMask[rangeIndex]));
        setParameterNormalisedValue(*parameter, parameter->convertTo0to1(enabled ? 1.0f : 0.0f));
    }

    if (config.markParametersDirty != nullptr)
        config.markParametersDirty();
}

void CrossoverModuleComponent::synchroniseManualSoloMaskFromParameters()
{
    if (autoSoloEnabled)
        return;

    const auto activeRangeCount = getActiveRangeCount();

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        manualSoloMask[rangeIndex] = rangeIndex < activeRangeCount && isRangeSoloEnabled(rangeIndex);
}

bool CrossoverModuleComponent::isRangeSoloEnabled(const size_t rangeIndex) const noexcept
{
    return rangeIndex < soloParameters.size()
        && soloParameters[rangeIndex] != nullptr
        && soloParameters[rangeIndex]->getValue() >= 0.5f;
}

void CrossoverModuleComponent::updateMonitorButtons()
{
    const auto activeRangeCount = getActiveRangeCount();

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        if (auto* button = monitorButtons[rangeIndex].get())
        {
            const auto isActiveRange = rangeIndex < activeRangeCount;
            button->setVisible(true);
            button->setEnabled(isActiveRange);
            button->setAlpha(1.0f);
            button->setToggleState(! crossoverSettingsActive && rangeIndex == visibleRangeIndex, juce::dontSendNotification);
        }

        if (auto* page = rangePages[rangeIndex].get())
            page->refreshExternalState();
    }

    if (auto* button = monitorButtons[numRanges].get())
    {
        button->setVisible(true);
        button->setEnabled(true);
        button->setAlpha(1.0f);
        button->setToggleState(crossoverSettingsActive, juce::dontSendNotification);
    }
}

void CrossoverModuleComponent::updatePageVisibility()
{
    const auto activeRangeCount = getActiveRangeCount();

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        if (auto* page = rangePages[rangeIndex].get())
            page->setVisible(! crossoverSettingsActive && rangeIndex < activeRangeCount && rangeIndex == visibleRangeIndex);
    }

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->setVisible(crossoverSettingsActive);

    updatePinnedHeaderComponent();
    updatePinnedTailComponent();
    updatePageViewport();
}

void CrossoverModuleComponent::updatePinnedHeaderComponent()
{
    auto* currentPage = getCurrentPageComponent();
    auto* nextPinnedHeader = currentPage != nullptr ? currentPage->getPinnedHeaderComponent() : nullptr;

    if (pinnedHeaderComponent != nextPinnedHeader)
    {
        if (pinnedHeaderComponent != nullptr)
        {
            pinnedHeaderComponent->setVisible(false);
            removeChildComponent(pinnedHeaderComponent);
        }

        pinnedHeaderComponent = nextPinnedHeader;

        if (pinnedHeaderComponent != nullptr)
            addAndMakeVisible(*pinnedHeaderComponent);
    }

    if (pinnedHeaderComponent != nullptr)
        pinnedHeaderComponent->setVisible(nextPinnedHeader != nullptr);
}

void CrossoverModuleComponent::updatePinnedTailComponent()
{
    auto* currentPage = getCurrentPageComponent();
    auto* nextPinnedTail = currentPage != nullptr ? currentPage->getPinnedTailComponent() : nullptr;

    if (pinnedTailComponent != nextPinnedTail)
    {
        if (pinnedTailComponent != nullptr)
        {
            pinnedTailComponent->setVisible(false);
            removeChildComponent(pinnedTailComponent);
        }

        pinnedTailComponent = nextPinnedTail;

        if (pinnedTailComponent != nullptr)
            addAndMakeVisible(*pinnedTailComponent);
    }

    if (pinnedTailComponent != nullptr)
        pinnedTailComponent->setVisible(nextPinnedTail != nullptr);
}

CrossoverModulePage* CrossoverModuleComponent::getCurrentPageComponent() const noexcept
{
    if (crossoverSettingsActive)
        return crossoverSettingsPage.get();

    return visibleRangeIndex < rangePages.size() ? rangePages[visibleRangeIndex].get()
                                               : nullptr;
}

int CrossoverModuleComponent::getCurrentPagePreferredHeight() const noexcept
{
    if (crossoverSettingsActive)
        return crossoverSettingsPage != nullptr ? crossoverSettingsPage->getPreferredHeight() : 0;

    return visibleRangeIndex < rangePages.size() && rangePages[visibleRangeIndex] != nullptr
        ? rangePages[visibleRangeIndex]->getPreferredHeight()
        : 0;
}

int CrossoverModuleComponent::getCurrentPinnedHeaderHeight() const noexcept
{
    if (auto* currentPage = getCurrentPageComponent())
        return currentPage->getPinnedHeaderHeight();

    return 0;
}

int CrossoverModuleComponent::getCurrentPinnedTailHeight() const noexcept
{
    if (auto* currentPage = getCurrentPageComponent())
        return currentPage->getPinnedTailHeight();

    return 0;
}

void CrossoverModuleComponent::updatePageViewport()
{
    auto* currentPage = getCurrentPageComponent();

    if (currentPage == nullptr)
        return;

    const auto viewportBounds = pageViewport.getLocalBounds();

    if (viewportBounds.isEmpty())
        return;

    const auto preserveScroll = pageViewport.getViewedComponent() == currentPage;
    const auto previousScrollY = preserveScroll ? pageViewport.getViewPositionY() : restoredPageScrollY;

    if (! preserveScroll)
        pageViewport.setViewedComponent(currentPage, false);

    const auto pageHeight = juce::jmax(viewportBounds.getHeight(), getCurrentPagePreferredHeight());
    currentPage->setSize(viewportBounds.getWidth(), pageHeight);
    const auto maxScrollY = juce::jmax(0, pageHeight - viewportBounds.getHeight());
    pageViewport.setViewPosition(0, juce::jlimit(0, maxScrollY, previousScrollY));
    pageScrollRestored = true;
}

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

bool CrossoverModuleComponent::setParameterPlainValue(const juce::String& parameterId, const float plainValue)
{
    if (auto* parameter = valueTreeState.getParameter(parameterId))
        return setParameterNormalisedValue(*parameter, parameter->convertTo0to1(plainValue));

    return false;
}

bool CrossoverModuleComponent::swapParameterPlainValues(const juce::String& firstParameterId,
                                                        const juce::String& secondParameterId)
{
    auto* firstParameter = valueTreeState.getParameter(firstParameterId);
    auto* secondParameter = valueTreeState.getParameter(secondParameterId);

    if (firstParameter == nullptr || secondParameter == nullptr)
        return false;

    const auto firstValue = firstParameter->convertFrom0to1(firstParameter->getValue());
    const auto secondValue = secondParameter->convertFrom0to1(secondParameter->getValue());

    if (std::abs(firstValue - secondValue) <= 1.0e-6f)
        return false;

    if (config.undoManager != nullptr)
        config.undoManager->beginNewTransaction();

    firstParameter->beginChangeGesture();
    secondParameter->beginChangeGesture();
    firstParameter->setValueNotifyingHost(firstParameter->convertTo0to1(secondValue));
    secondParameter->setValueNotifyingHost(secondParameter->convertTo0to1(firstValue));
    secondParameter->endChangeGesture();
    firstParameter->endChangeGesture();

    if (config.markParametersDirty != nullptr)
        config.markParametersDirty();

    return true;
}

bool CrossoverModuleComponent::constrainCrossoverFrequency(const size_t crossoverIndex)
{
    const auto activeSplitCount = getActiveSplitCount();

    if (crossoverIndex >= activeSplitCount || crossoverIndex >= crossoverSuffixes.size())
        return false;

    const auto parameterId = config.makeCrossoverParameterId(crossoverSuffixes[crossoverIndex]);
    auto* parameter = valueTreeState.getParameter(parameterId);

    if (parameter == nullptr)
        return false;

    auto lowerBound = parameter->convertFrom0to1(0.0f);
    auto upperBound = parameter->convertFrom0to1(1.0f);

    if (crossoverIndex > 0)
    {
        const auto previousId = config.makeCrossoverParameterId(crossoverSuffixes[crossoverIndex - 1]);

        if (auto* previousParameter = valueTreeState.getParameter(previousId))
            lowerBound = juce::jmax(lowerBound,
                                    previousParameter->convertFrom0to1(previousParameter->getValue()) + minCrossoverFrequencyGapHz);
    }

    if (crossoverIndex + 1 < activeSplitCount)
    {
        const auto nextId = config.makeCrossoverParameterId(crossoverSuffixes[crossoverIndex + 1]);

        if (auto* nextParameter = valueTreeState.getParameter(nextId))
            upperBound = juce::jmin(upperBound,
                                    nextParameter->convertFrom0to1(nextParameter->getValue()) - minCrossoverFrequencyGapHz);
    }

    const auto currentValue = parameter->convertFrom0to1(parameter->getValue());
    const auto constrainedValue = juce::jlimit(lowerBound, juce::jmax(lowerBound, upperBound), currentValue);

    if (std::abs(currentValue - constrainedValue) <= 1.0e-6f)
        return false;

    return setParameterPlainValue(parameterId, constrainedValue);
}

bool CrossoverModuleComponent::setParameterNormalisedValue(juce::RangedAudioParameter& parameter, const float normalisedValue)
{
    const auto value = juce::jlimit(0.0f, 1.0f, normalisedValue);
    const auto wasDifferent = std::abs(parameter.getValue() - value) > 1.0e-6f;

    if (! wasDifferent)
        return false;

    if (config.undoManager != nullptr)
        config.undoManager->beginNewTransaction();

    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(value);
    parameter.endChangeGesture();

    if (config.markParametersDirty != nullptr)
        config.markParametersDirty();

    return true;
}

bool CrossoverModuleComponent::assignButtonToHostSlot(const juce::String& parameterId,
                                                       const juce::String& fallbackName,
                                                       const BoxTextButton* button)
{
    if (config.assignHostSlot == nullptr)
        return false;

    if (auto* parameter = valueTreeState.getParameter(parameterId))
    {
        return config.assignHostSlot(parameterId,
                                     button != nullptr ? button->getButtonText() : fallbackName,
                                     parameter->getValue());
    }

    return false;
}

void CrossoverModuleComponent::clearFocus()
{
    shell_parameter_focus::clearFocus(*this);

    if (config.clearKeyboardFocus != nullptr)
        config.clearKeyboardFocus();
    else
        clearKeyboardFocus(*this);
}
