#include "../shell/EditorSetup.h"
#include "Editor.h"
#include "Page.h"
#include "RangePage.h"
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
    graphics.fillAll(uiBackground);
}

void CrossoverModuleComponent::resized()
{
    auto bounds = getLocalBounds();
    if (config.moduleKey == "dyn") bounds.reduce(dynControlGroupOutset, 0);
    if (config.showCrossoverNavigation)
    {
        auto monitorRow = bounds.removeFromTop(rowHeight);
        const auto buttonCount = static_cast<int>(monitorButtons.size());
        const auto totalGapWidth = parameterGap * (buttonCount - 1);
        const auto rangeButtonCount = buttonCount - 1;
        const auto rangeButtonsWidth = juce::jmax(0, monitorRow.getWidth() - totalGapWidth - iconControlSize);
        const auto baseButtonWidth = rangeButtonsWidth / rangeButtonCount;
        auto remainder = rangeButtonsWidth - baseButtonWidth * rangeButtonCount;

        for (int index = 0; index < buttonCount; ++index)
        {
            const auto isSettingsButton = index == rangeButtonCount;
            const auto buttonWidth = isSettingsButton
                ? iconControlSize
                : baseButtonWidth + (remainder > 0 ? 1 : 0);

            if (auto& button = monitorButtons[static_cast<size_t>(index)])
                button->setBounds(monitorRow.removeFromLeft(buttonWidth));
            else
                monitorRow.removeFromLeft(buttonWidth);

            if (! isSettingsButton)
            {
                monitorRow.removeFromLeft(parameterGap);
                remainder = juce::jmax(0, remainder - 1);
            }
        }

        if (! bounds.isEmpty())
            bounds.removeFromTop(verticalGap);
    }

    if (instanceHeading != nullptr && headerSoloButton != nullptr)
    {
        auto headerRow = bounds.removeFromTop(rowHeight);
        if (crossoverSettingsActive)
        {
            const auto leftWidth = juce::jmax(0, (headerRow.getWidth() - parameterGap) / 2);
            instanceHeading->setBounds(headerRow.removeFromLeft(leftWidth));
            headerRow.removeFromLeft(juce::jmin(parameterGap, headerRow.getWidth()));
            headerSoloButton->setBounds({});
        }
        else
        {
            const auto sideWidth = juce::jmin(120, juce::jmax(0, (headerRow.getWidth() - 2 * parameterGap) / 2));
            instanceHeading->setBounds(headerRow.removeFromLeft(sideWidth));
            headerRow.removeFromLeft(juce::jmin(parameterGap, headerRow.getWidth()));
            const auto soloWidth = juce::jmax(0, headerRow.getWidth() - sideWidth - parameterGap);
            headerSoloButton->setBounds(headerRow.removeFromLeft(soloWidth));
            headerRow.removeFromLeft(juce::jmin(parameterGap, headerRow.getWidth()));
        }
        if (moduleAddButton != nullptr)
            moduleAddButton->setBounds(headerRow);
        if (moduleTitleButton != nullptr)
            moduleTitleButton->setBounds(headerRow);

        if (! bounds.isEmpty())
            bounds.removeFromTop(verticalGap);
    }

    updatePinnedComponents();
    const auto pinnedTailHeight = getCurrentPinnedTailHeight();
    auto pinnedTailBounds = bounds.removeFromBottom(pinnedTailHeight);

    updatePinnedComponents();
    const auto pinnedHeaderHeight = getCurrentPinnedHeaderHeight();
    auto pinnedHeaderBounds = bounds.removeFromTop(pinnedHeaderHeight);

    if (pinnedHeaderComponent != nullptr)
        pinnedHeaderComponent->setBounds(pinnedHeaderBounds);

    if (pinnedHeaderHeight > 0 && ! bounds.isEmpty())
        bounds.removeFromTop(verticalGap);

    pageViewport.setBounds(config.moduleKey == "dyn" && ! crossoverSettingsActive
        ? bounds.expanded(dynControlGroupOutset, 0) : bounds);

    if (pinnedTailComponent != nullptr)
    {
        pinnedTailComponent->setBounds(pinnedTailBounds);
        if (auto* currentPage = getCurrentPageComponent())
            currentPage->layoutPinnedTail();
    }
    updatePageViewport();
}

void CrossoverModuleComponent::mouseDown(const juce::MouseEvent& event)
{
    clearFocus();
    if(event.mods.isPopupMenu()) showBlockArrange();
}

void CrossoverModuleComponent::showBlockArrange()
{
    if(auto* page=dynamic_cast<CrossoverRangePage*>(getCurrentPageComponent())) page->showBlockArrange();
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
    saveUiState();
}

void CrossoverModuleComponent::refreshRangeControls()
{
    for (auto& page : rangePages)
        if (page != nullptr) page->refreshExternalState();
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
    if(config.getBlockOrder) refreshRangeControls();

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateMonitorButtons();
    updateHeaderSoloState();
    updatePageViewport();
}

int CrossoverModuleComponent::getPreferredHeight() const noexcept
{
    const auto pinnedHeaderHeight = getCurrentPinnedHeaderHeight();

    return (config.showCrossoverNavigation ? rowHeight + verticalGap : 0)
        + (config.showCrossoverSolo ? rowHeight + verticalGap : 0)
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

    updateHeaderSoloState();
    updatePinnedComponents();
    updatePageViewport();
}

void CrossoverModuleComponent::updatePinnedComponents()
{
    auto* currentPage = getCurrentPageComponent();
    const auto update = [this] (juce::Component*& pinned, juce::Component* next)
    {
        if (pinned != next)
        {
            if (pinned != nullptr)
            {
                pinned->setVisible(false);
                removeChildComponent(pinned);
            }

            pinned = next;
            if (pinned != nullptr)
                addAndMakeVisible(*pinned);
        }

        if (pinned != nullptr)
            pinned->setVisible(true);
    };

    update(pinnedHeaderComponent, currentPage != nullptr ? currentPage->getPinnedHeaderComponent() : nullptr);
    update(pinnedTailComponent, currentPage != nullptr ? currentPage->getPinnedTailComponent() : nullptr);
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
    if (auto* currentPage = getCurrentPageComponent())
        return currentPage->getPreferredHeight();

    return 0;
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
    {
        pageViewport.setViewedComponent(currentPage, false);

        if (auto* holder = currentPage != nullptr ? currentPage->getParentComponent() : nullptr)
            holder->setName("viewport-content");
    }

    const auto pageHeight = juce::jmax(viewportBounds.getHeight(), getCurrentPagePreferredHeight());
    currentPage->setSize(viewportBounds.getWidth(), pageHeight);
    const auto maxScrollY = juce::jmax(0, pageHeight - viewportBounds.getHeight());
    pageViewport.setViewPosition(0, juce::jlimit(0, maxScrollY, previousScrollY));
    pageScrollRestored = true;
}
