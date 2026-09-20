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

