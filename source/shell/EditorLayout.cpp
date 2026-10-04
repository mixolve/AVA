#include "Editor.h"
#include "FilterSection.h"
#include "../crossover/Component.h"
#include "UiConstants.h"
#include "PresetSections.h"
#include "../routing/Panel.h"
#include "OscPanel.h"

void AvaAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(uiBackground);
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
            totalHeight += verticalGap + section->orderControl->getPreferredHeight();
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

    constexpr int globalButtonGap = 6;
    auto row = bounds.removeFromTop(rowHeight);

    routingButton->setBounds(row.removeFromLeft(iconControlSize));
    row.removeFromLeft(globalButtonGap);
    oscButton->setBounds(row.removeFromLeft(iconControlSize));
    row.removeFromLeft(globalButtonGap);
    hostButton->setBounds(row.removeFromLeft(iconControlSize));
    row.removeFromLeft(globalButtonGap);

    clipButton->setBounds(row.removeFromRight(iconControlSize));
    row.removeFromRight(globalButtonGap);
    globalBypassButton->setBounds(row.removeFromRight(iconControlSize));
    row.removeFromRight(globalButtonGap);
    topLines->setBounds(row.reduced(0, 1));

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

void AvaAudioProcessorEditor::layoutBottomControlsSection(juce::Rectangle<int>& bounds)
{
    auto row = bounds.removeFromBottom(rowHeight);
    if (! bounds.isEmpty())
        bounds.removeFromBottom(juce::jmin(verticalGap, bounds.getHeight()));

    abSlotAButton->setBounds(row.removeFromLeft(iconControlSize));
    row.removeFromLeft(uiGap);
    abSwitchButton->setBounds(row.removeFromLeft(iconControlSize));
    row.removeFromLeft(uiGap);
    abSlotBButton->setBounds(row.removeFromLeft(iconControlSize));
    row.removeFromLeft(uiGap);

    footerTab->setBounds(row.removeFromRight(iconControlSize));
    row.removeFromRight(uiGap);
    redoButton->setBounds(row.removeFromRight(iconControlSize));
    row.removeFromRight(uiGap);
    undoButton->setBounds(row.removeFromRight(iconControlSize));
    row.removeFromRight(uiGap);
    bottomLines->setBounds(row.reduced(0, 1));
}

void AvaAudioProcessorEditor::layoutFooter(juce::Rectangle<int>& bounds)
{
    if (focusedParameterControl == nullptr)
        return;

    if (routingExpanded || oscExpanded)
    {
        focusedParameterControl->setBounds({});
        return;
    }

    auto focusedBounds = bounds.removeFromBottom(footerHeight);
    focusedParameterControl->setBounds(focusedBounds);
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
    if (topLines != nullptr) topLines->toFront(false);
    if (bottomLines != nullptr) bottomLines->toFront(false);
    if (routingButton != nullptr) routingButton->toFront(false);
    if (oscButton != nullptr) oscButton->toFront(false);
    if (moduleAddButton != nullptr) moduleAddButton->toFront(false);
    if (hostButton != nullptr) hostButton->toFront(false);
    if (fftDeltaButton != nullptr) fftDeltaButton->toFront(false);
    footerTab->toFront(false);

    if (routingPanel != nullptr && routingExpanded)
        routingPanel->toFront(false);
    if (oscPanel != nullptr && oscExpanded)
        oscPanel->toFront(false);

    if (focusedParameterControl != nullptr)
        focusedParameterControl->toFront(false);

    if (verticalResizeHandle != nullptr)
        verticalResizeHandle->toFront(false);
    if (horizontalResizeHandle != nullptr)
        horizontalResizeHandle->toFront(false);

    if (textPromptOverlay != nullptr)
    {
        textPromptOverlay->setBounds(getLocalBounds());
        textPromptOverlay->toFront(false);
    }

    if (activeInstanceEditor != nullptr)
    {
        activeInstanceEditor->setBounds(getLocalBounds());
        activeInstanceEditor->toFront(false);
    }

    storeEditorStateToValueTree();
}

void AvaAudioProcessorEditor::resized()
{
    if (moduleAddButton == nullptr
        || clipButton == nullptr
        || presetsSection == nullptr
        || globalBypassButton == nullptr
        || topLines == nullptr
        || bottomLines == nullptr
        || routingButton == nullptr
        || oscButton == nullptr
        || oscPanel == nullptr
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

    if (verticalResizeHandle != nullptr)
        verticalResizeHandle->setBounds(bounds.withTop(juce::jmax(0, bounds.getBottom() - resizeHandleThickness)));
    if (horizontalResizeHandle != nullptr)
        horizontalResizeHandle->setBounds(bounds.withLeft(juce::jmax(0, bounds.getRight() - resizeHandleThickness)));

    resetAnalyserPanelBounds();

    const auto editorInsetX = getEditorInsetX(bounds.getWidth());
    const auto totalHeight = bounds.getHeight();
    const auto editorInsetTop = getEditorInsetTop(totalHeight);
    const auto editorInsetBottom = getEditorInsetBottom(totalHeight);

    bounds.removeFromLeft(editorInsetX);
    bounds.removeFromRight(editorInsetX);

    bounds.removeFromBottom(editorInsetBottom);
    bounds.removeFromTop(editorInsetTop);

    layoutBottomControlsSection(bounds);
    layoutFooter(bounds);
    layoutGlobalControlsSection(bounds);
    if (routingExpanded)
    {
        if (routingPanel != nullptr)
            routingPanel->setBounds(bounds);
        finalizeLayout();
        return;
    }

    if (routingPanel != nullptr)
        routingPanel->setBounds({});
    if (oscExpanded)
    {
        oscPanel->setBounds(bounds);
        finalizeLayout();
        return;
    }
    oscPanel->setBounds({});
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
        finalizeLayout();
        return;
    }

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

void AvaAudioProcessorEditor::layoutCrossoverSection(juce::Rectangle<int>& bounds)
{
    auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get());

    if (editor == nullptr)
        return;

    const auto reservedPotentiometerGap = editor->isCrossoverSettingsSelected()
        ? viewportToPotentiometerGap
        : 0;
    const auto availableHeight = juce::jmax(0,
                                            bounds.getHeight()
                                                - reservedPotentiometerGap);
    const auto sectionHeight = juce::jmin(availableHeight, editor->getPreferredHeight());
    editor->setBounds(bounds.removeFromTop(sectionHeight));
    editor->setVisible(sectionHeight > 0);
}

void AvaAudioProcessorEditor::layoutModuleEditorContent(juce::Rectangle<int>& bounds)
{
    auto contentBounds = bounds;
    contentBounds.removeFromBottom(viewportToPotentiometerGap);

    if (tlsModuleEditor != nullptr)
    {
        tlsModuleEditor->setBounds(contentBounds);
        tlsModuleEditor->setVisible(tlsModuleLoaded);
    }

    if (dynModuleEditor != nullptr)
    {
        dynModuleEditor->setBounds(contentBounds);
        dynModuleEditor->setVisible(dynModuleLoaded);
    }

    if (trsModuleEditor != nullptr)
    {
        trsModuleEditor->setBounds(contentBounds);
        trsModuleEditor->setVisible(trsModuleLoaded);
    }

}
