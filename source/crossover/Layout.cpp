#include "../shell/EditorSetup.h"
#include "Editor.h"
#include "Page.h"
#include "../shell/Editor.h"
#include "UiSupport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/ParameterControl.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace crossover_ui;

void CrossoverModuleComponent::resized()
{
    auto bounds = getLocalBounds();
    pageViewport.setBounds(bool(getProperties().getWithDefault("headerOnly", false)) || bool(getProperties().getWithDefault("sharedMainControls", false)) ? juce::Rectangle<int>() : bounds);
    updatePageViewport();
}

void CrossoverModuleComponent::mouseDown(const juce::MouseEvent&)
{
    clearFocus();
}

void CrossoverModuleComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (bool(getProperties().getWithDefault("stackedModule", false)))
    {
        if (auto* editor = findParentComponentOfClass<AvaAudioProcessorEditor>())
            editor->mouseWheelMove(event.getEventRelativeTo(editor), wheel);
        return;
    }
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
    saveUiState();
}

void CrossoverModuleComponent::refreshRangeControls()
{
    for (auto& page : rangePages)
        if (page != nullptr && page->isVisible()) page->refreshExternalState();
}

void CrossoverModuleComponent::refreshExternalState()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (!externalStateDirty.exchange(false) && now - lastAvailabilityRefresh < 1000) {
        if (pageScrollRestored && pageViewport.getViewPositionY() != restoredPageScrollY) saveUiState();
        return;
    }
    lastAvailabilityRefresh = now;
    if (! restoreUiStateIfChanged()
        && pageScrollRestored
        && pageViewport.getViewPositionY() != restoredPageScrollY)
        saveUiState();

    // MODULES reuses this editor for CHAIN and DYN. Their UI state is
    // already bound to parameters; refreshing navigation and the viewport for
    // every knob tick reflows the page while JUCE is processing its drag.
    // Refresh only dependent control policies here. A genuine visibility
    // change still triggers the page's own resized() path.
    if (config.moduleKey == "chain" || config.moduleKey == "dyn")
    {
        for (auto& page : rangePages)
            if (page != nullptr) page->refreshExternalState();
        return;
    }

    synchroniseManualSoloMaskFromParameters();
    if(config.moduleKey == "chain") refreshRangeControls();

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateRangeNavigation();
    updateSoloState();
    updatePageViewport();
}

int CrossoverModuleComponent::getPreferredHeight() const noexcept
{
    return (config.showCrossoverNavigation || config.showCrossoverSolo ? getSquareControlSize(*this) + getUiGap(*this) : 0)
         + getCurrentPagePreferredHeight();
}

void CrossoverModuleComponent::updatePageVisibility()
{
    const auto activeRangeCount = getActiveRangeCount();

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        if (auto* page = rangePages[rangeIndex].get()) {
            const auto show = rangeIndex < activeRangeCount && rangeIndex == visibleRangeIndex;
            if (show && !page->isVisible()) page->refreshExternalState();
            page->setVisible(show);
        }
    }

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->setVisible(config.showCrossoverControls);

    updateSoloState();
    updatePageViewport();
}

CrossoverModulePage* CrossoverModuleComponent::getCurrentPageComponent() const noexcept
{
    return visibleRangeIndex < rangePages.size() ? rangePages[visibleRangeIndex].get()
                                               : nullptr;
}

int CrossoverModuleComponent::getCurrentPagePreferredHeight() const noexcept
{
    if (auto* currentPage = getCurrentPageComponent())
        return currentPage->getPreferredHeight();

    return 0;
}

void CrossoverModuleComponent::updatePageViewport()
{
    if (bool(getProperties().getWithDefault("flatBlocks", false)) || bool(getProperties().getWithDefault("sharedMainControls", false))) return;
    auto* currentPage = getCurrentPageComponent();

    if (currentPage == nullptr)
        return;

    const auto viewportBounds = pageViewport.getLocalBounds();

    if (viewportBounds.isEmpty())
        return;

    const auto preserveScroll = pageViewport.getViewedComponent() == currentPage;
    const auto previousScrollY = preserveScroll ? pageViewport.getViewPositionY() : restoredPageScrollY;

    if (! preserveScroll)
    {
        pageViewport.setViewedComponent(currentPage, false);

        if (auto* holder = currentPage != nullptr ? currentPage->getParentComponent() : nullptr)
            holder->setName("viewport-content");
    }

    currentPage->getProperties().set("contentRightInset", getProperties().getWithDefault("contentRightInset", 0));
    const auto pageWidth = viewportBounds.getWidth();
    currentPage->setSize(pageWidth, currentPage->getHeight());
    const auto pageHeight = juce::jmax(viewportBounds.getHeight(), getCurrentPagePreferredHeight());
    currentPage->setSize(pageWidth, pageHeight);
    pageViewport.setViewPosition(0, juce::jlimit(0, juce::jmax(0, pageHeight - viewportBounds.getHeight()), previousScrollY));
    pageScrollRestored = true;
}
