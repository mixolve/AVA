#include "EditorFilterSection.h"
#include "../crossover/ModuleComponent.h"
#include "UiConstants.h"
#include "EditorPresetSections.h"

void AvaAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

juce::Rectangle<int> AvaAudioProcessorEditor::getInfoPromptAnchorBounds() const noexcept
{
    if (footerTab != nullptr && ! footerTab->getBounds().isEmpty())
        return footerTab->getBounds();

    if (clipButton != nullptr
        && clipButton->isVisible()
        && ! clipButton->getBounds().isEmpty())
        return clipButton->getBounds();

    return {};
}

juce::Rectangle<int> AvaAudioProcessorEditor::getInfoPromptVisibleBounds() const noexcept
{
    auto visibleBounds = getLocalBounds();

    if (clipButton != nullptr
        && clipButton->isVisible()
        && ! clipButton->getBounds().isEmpty())
    {
        visibleBounds.setTop(clipButton->getY());
    }

    return visibleBounds;
}

int AvaAudioProcessorEditor::getFilterContentHeight() const
{
    if (addFilterButton == nullptr)
        return 0;

    for (const auto& section : filterSections)
    {
        if (section == nullptr)
            return 0;
    }

    const auto activeFilterCount = getActiveFilterCount();
    auto totalHeight = 0;

    for (int displayIndex = 0; displayIndex < activeFilterCount; ++displayIndex)
    {
        const auto filterIndex = getFilterIndexForOrderPosition(displayIndex);
        if (filterIndex < 0)
            continue;

        totalHeight += rowHeight;

        const auto* section = filterSections[static_cast<size_t>(filterIndex)].get();

        if (section != nullptr && section->expanded)
        {
            totalHeight += verticalGap + section->typeControl->getPreferredHeight();
            totalHeight += verticalGap + section->placeControl->getPreferredHeight();
            totalHeight += verticalGap + section->slopeControl->getPreferredHeight();
            totalHeight += verticalGap + section->frequencyControl->getPreferredHeight();
            totalHeight += verticalGap + section->bandwidthControl->getPreferredHeight();
            totalHeight += verticalGap + section->gainControl->getPreferredHeight();
        }

        totalHeight += verticalGap;
    }

    return totalHeight + verticalGap;
}

int AvaAudioProcessorEditor::getActiveFilterContentHeight() const
{
    if (tlsModuleLoaded || dynModuleLoaded || trsModuleLoaded)
        return 0;

    if (fftModuleLoaded)
    {
        return getFftMainContentHeight() + moduleContentBottomGap;
    }

    if (eqlModuleLoaded)
        return getFilterContentHeight();

    return 0;
}

void AvaAudioProcessorEditor::resetAnalyserPanelBounds()
{
    if (fftAnalyserComponent != nullptr)
        fftAnalyserComponent->setBounds({});
}

void AvaAudioProcessorEditor::layoutGlobalControlsSection(juce::Rectangle<int>& bounds)
{
    hostParametersViewport.setVisible(false);
    hostParametersViewport.setBounds({});
    hostParametersContent.setSize(0, 0);

    std::array<BoxTextButton*, 9> panelButtons {
        abSlotAButton.get(),
        abSwitchButton.get(),
        abSlotBButton.get(),
        undoButton.get(),
        redoButton.get(),
        globalBypassButton.get(),
        clipButton.get(),
        hostButton.get(),
        footerTab.get()
    };

    const auto buttonWidth = [] (const BoxTextButton* button)
    {
        return button != nullptr && button->usesIconOnlyContent() ? iconControlSize : rowHeight;
    };
    auto visibleButtonCount = 0;
    auto iconButtonCount = 0;
    auto textButtonCount = 0;

    for (auto* button : panelButtons)
    {
        if (button == nullptr || ! button->isVisible())
            continue;

        ++visibleButtonCount;
        if (button->usesIconOnlyContent())
            ++iconButtonCount;
        else
            ++textButtonCount;
    }

    const auto minimumSingleRowWidth = (iconButtonCount * iconControlSize)
        + (textButtonCount * rowHeight)
        + (juce::jmax(0, visibleButtonCount - 1) * parameterGap);
    const auto distributeTextButtons = textButtonCount > 0
        && bounds.getWidth() >= minimumSingleRowWidth;
    auto globalRowCount = 0;
    auto currentRowWidth = 0;

    if (distributeTextButtons)
    {
        globalRowCount = visibleButtonCount > 0 ? 1 : 0;
    }
    else for (auto* button : panelButtons)
    {
        if (button == nullptr)
            continue;

        button->setBounds({});

        if (! button->isVisible())
            continue;

        const auto additionalWidth = currentRowWidth == 0
            ? buttonWidth(button)
            : parameterGap + buttonWidth(button);

        if (currentRowWidth > 0 && currentRowWidth + additionalWidth > bounds.getWidth())
        {
            ++globalRowCount;
            currentRowWidth = buttonWidth(button);
        }
        else
        {
            currentRowWidth += additionalWidth;
        }
    }

    if (currentRowWidth > 0)
        ++globalRowCount;

    if (globalRowCount > 0)
    {
        const auto globalControlsHeight = (globalRowCount * rowHeight)
            + ((globalRowCount - 1) * verticalGap);
        auto globalControlsBounds = bounds.removeFromTop(globalControlsHeight);
        auto rowBounds = globalControlsBounds.withHeight(rowHeight);

        if (distributeTextButtons)
        {
            const auto textButtonWidthBudget = rowBounds.getWidth()
                - (iconButtonCount * iconControlSize)
                - (juce::jmax(0, visibleButtonCount - 1) * parameterGap);
            const auto textButtonWidth = textButtonWidthBudget / textButtonCount;
            auto textButtonWidthRemainder = textButtonWidthBudget % textButtonCount;
            auto textButtonIndex = 0;

            for (auto* button : panelButtons)
            {
                if (button == nullptr || ! button->isVisible())
                    continue;

                const auto width = button->usesIconOnlyContent()
                    ? iconControlSize
                    : textButtonWidth + (textButtonIndex++ < textButtonWidthRemainder ? 1 : 0);
                button->setBounds(rowBounds.removeFromLeft(width));

                if (! rowBounds.isEmpty())
                    rowBounds.removeFromLeft(parameterGap);
            }
        }
        else
        {
            auto usedRowWidth = 0;

            for (auto* button : panelButtons)
            {
                if (button == nullptr || ! button->isVisible())
                    continue;

                const auto width = buttonWidth(button);
                const auto additionalWidth = usedRowWidth == 0 ? width : parameterGap + width;

                if (usedRowWidth > 0 && usedRowWidth + additionalWidth > rowBounds.getWidth())
                {
                    rowBounds.translate(0, rowHeight + verticalGap);
                    usedRowWidth = 0;
                }

                const auto x = rowBounds.getX() + (usedRowWidth == 0 ? 0 : usedRowWidth + parameterGap);
                button->setBounds(x, rowBounds.getY(), width, rowHeight);
                usedRowWidth += usedRowWidth == 0 ? width : parameterGap + width;
            }
        }
    }

    if (! bounds.isEmpty())
        bounds.removeFromTop(globalToFilterGap);

    if (! hostParametersExpanded)
    {
        return;
    }

    const auto minimumBelowGlobalControls = addFilterToFooterGap;
    const auto hostPanelViewportHeight = juce::jmax(0, bounds.getHeight() - minimumBelowGlobalControls);
    auto hostPanelBounds = bounds.removeFromTop(hostPanelViewportHeight);

    if (! hostPanelBounds.isEmpty())
    {
        if (! hostPanelBounds.isEmpty())
            hostPanelBounds.removeFromBottom(verticalGap);

        const auto slotCount = static_cast<int>(hostSlotButtons.size());
        const auto hostContentHeight = slotCount > 0
            ? (slotCount * rowHeight) + ((slotCount - 1) * verticalGap)
            : 0;

        auto hostViewportBounds = hostPanelBounds;
        hostViewportBounds.setHeight(juce::jmin(hostViewportBounds.getHeight(), hostContentHeight));
        hostParametersViewport.setVisible(true);
        hostParametersViewport.setBounds(hostViewportBounds);

        hostParametersContent.setSize(hostViewportBounds.getWidth(),
                                       juce::jmax(hostViewportBounds.getHeight(), hostContentHeight));

        auto hostContentBounds = hostParametersContent.getLocalBounds();

        for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
        {
            auto* slotNameField = hostSlotNameFields[static_cast<size_t>(slotIndex)].get();
            auto* slotButton = hostSlotButtons[static_cast<size_t>(slotIndex)].get();

            if (slotNameField == nullptr || slotButton == nullptr)
                continue;

            auto slotBounds = hostContentBounds.removeFromTop(rowHeight);
            const auto slotNameWidth = (rowHeight * 2) + uiGap + 4;
            auto slotNameBounds = slotBounds.removeFromLeft(slotNameWidth);
            slotBounds.removeFromLeft(parameterGap);
            slotNameField->setBounds(slotNameBounds);
            slotButton->setBounds(slotBounds);

            if (! hostContentBounds.isEmpty())
                hostContentBounds.removeFromTop(verticalGap);
        }

        if (! bounds.isEmpty())
            bounds.removeFromTop(globalToFilterGap);
    }
}

void AvaAudioProcessorEditor::layoutFooter(juce::Rectangle<int>& bounds)
{
    if (focusedParameterControl == nullptr)
        return;

    auto focusedBounds = bounds.removeFromBottom(footerHeight);
    focusedParameterControl->setBounds(focusedBounds);
}

void AvaAudioProcessorEditor::layoutModuleTitle(juce::Rectangle<int>& bounds)
{
    if (moduleTitle == nullptr)
        return;

    moduleTitle->setBounds({});

    if (! moduleTitle->isVisible())
        return;

    auto rowBounds = bounds.removeFromTop(rowHeight);
    moduleTitle->setBounds(rowBounds);

    if (! bounds.isEmpty())
        bounds.removeFromTop(verticalGap);
}

void AvaAudioProcessorEditor::finalizeLayout() noexcept
{
    shell_parameter_focus::clearFocusIfNotShowing(*this);

    if (moduleTitle != nullptr)
        moduleTitle->toFront(false);

    clipButton->toFront(false);
    if (undoButton != nullptr) undoButton->toFront(false);
    if (redoButton != nullptr) redoButton->toFront(false);
    if (abSlotAButton != nullptr) abSlotAButton->toFront(false);
    if (abSwitchButton != nullptr) abSwitchButton->toFront(false);
    if (abSlotBButton != nullptr) abSlotBButton->toFront(false);
    if (globalBypassButton != nullptr) globalBypassButton->toFront(false);
    if (moduleAddButton != nullptr) moduleAddButton->toFront(false);
    if (hostButton != nullptr) hostButton->toFront(false);
    if (fftDeltaButton != nullptr) fftDeltaButton->toFront(false);
    footerTab->toFront(false);

    if (focusedParameterControl != nullptr)
        focusedParameterControl->toFront(false);

    if (horizontalResizeHandle != nullptr)
        horizontalResizeHandle->toFront(false);

    if (verticalResizeHandle != nullptr)
        verticalResizeHandle->toFront(false);

    if (textPromptOverlay != nullptr)
    {
        textPromptOverlay->setBounds(getLocalBounds());
        textPromptOverlay->toFront(false);
    }

    storeEditorStateToValueTree();
}

void AvaAudioProcessorEditor::resized()
{
    if (moduleAddButton == nullptr
        || clipButton == nullptr
        || presetsSection == nullptr
        || globalBypassButton == nullptr
        || undoButton == nullptr
        || redoButton == nullptr
        || abSlotAButton == nullptr
        || abSwitchButton == nullptr
        || abSlotBButton == nullptr
        || sortPlaceButton == nullptr
        || sortFreqButton == nullptr
        || sortDuoButton == nullptr
        || footerTab == nullptr)
        return;

    if (eqlModuleLoaded && addFilterButton == nullptr)
        return;

    for (const auto& section : filterSections)
    {
        if (eqlModuleLoaded && section == nullptr)
            return;
    }

    auto bounds = getLocalBounds();

    constexpr int resizeHandleThickness = 8;

    if (horizontalResizeHandle != nullptr)
        horizontalResizeHandle->setBounds(bounds.withLeft(juce::jmax(0, bounds.getRight() - resizeHandleThickness)));

    if (verticalResizeHandle != nullptr)
        verticalResizeHandle->setBounds(bounds.withTop(juce::jmax(0, bounds.getBottom() - resizeHandleThickness)));

    resetAnalyserPanelBounds();

    const auto editorInsetX = getEditorInsetX(bounds.getWidth());
    const auto totalHeight = bounds.getHeight();
    const auto editorInsetTop = getEditorInsetTop(totalHeight);
    const auto editorInsetBottom = getEditorInsetBottom(totalHeight);

    bounds.removeFromLeft(editorInsetX);
    bounds.removeFromRight(editorInsetX);

    bounds.removeFromBottom(editorInsetBottom);
    bounds.removeFromTop(editorInsetTop);

    layoutFooter(bounds);
    layoutGlobalControlsSection(bounds);
    layoutCrossoverSection(bounds);

    if (const auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
    {
        if (crossover->isCrossoverSettingsSelected())
        {
            finalizeLayout();
            return;
        }
    }

    if (! eqlModuleLoaded && ! fftModuleLoaded && ! tlsModuleLoaded && ! dynModuleLoaded && ! trsModuleLoaded)
    {
        layoutNoModuleState(bounds);
        finalizeLayout();
        return;
    }

    layoutModuleTitle(bounds);

    if (tlsModuleLoaded || dynModuleLoaded || trsModuleLoaded)
    {
        layoutModuleEditorContent(bounds);
        finalizeLayout();
        return;
    }

    if (fftModuleLoaded)
        layoutFftModuleSections(bounds);
    else
        layoutEqlModuleSections(bounds);

    finalizeLayout();
}
