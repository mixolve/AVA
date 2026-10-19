#include "Editor.h"
#include "FilterSection.h"
#include "../crossover/Editor.h"
#include "../crossover/DynControlGroups.h"
#include "Metrics.h"
#include "EditorSetup.h"
#include "PresetSection.h"
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
    const auto height = getFlatFilterContentHeight();
    return height;
}

int AvaAudioProcessorEditor::getFlatFilterContentHeight() const
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

        if (section != nullptr)
            totalHeight += section->visibleBodyHeight();

        totalHeight += verticalGap;
    }

    return totalHeight;
}

int AvaAudioProcessorEditor::getActiveFilterContentHeight() const
{
    if (tlsModuleLoaded || dynModuleLoaded)
        return 0;

    if (eqlModuleLoaded)
        return getFilterContentHeight();

    return 0;
}

void AvaAudioProcessorEditor::layoutGlobalControlsSection(juce::Rectangle<int>& bounds)
{
    hostParametersViewport.setVisible(false);
    hostParametersViewport.setBounds({});
    hostParametersContent.setSize(0, 0);

    constexpr int globalButtonGap = uiGap;
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
    globalSpacer->setBounds(row.removeFromRight(30));
    row.removeFromRight(globalButtonGap);
    topLines->setBounds(row);

    if (! bounds.isEmpty())
        bounds.removeFromTop(globalToFilterGap);

    if (! hostParametersExpanded)
    {
        return;
    }

    // The host list is the last section of the window: it fills everything that
    // is left above the bottom controls row, which already keeps its own gap.
    auto hostPanelBounds = bounds;
    bounds = {};

    if (! hostPanelBounds.isEmpty())
    {
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
    bottomLines->setBounds(row);
}

void AvaAudioProcessorEditor::finalizeLayout() noexcept
{
    if (moduleTitle != nullptr)
        moduleTitle->toFront(false);

    clipButton->toFront(false);
    if (undoButton != nullptr) undoButton->toFront(false);
    if (redoButton != nullptr) redoButton->toFront(false);
    if (abSlotAButton != nullptr) abSlotAButton->toFront(false);
    if (abSwitchButton != nullptr) abSwitchButton->toFront(false);
    if (abSlotBButton != nullptr) abSlotBButton->toFront(false);
    if (globalSpacer != nullptr) globalSpacer->toFront(false);
    if (globalBypassButton != nullptr) globalBypassButton->toFront(false);
    if (topLines != nullptr) topLines->toFront(false);
    if (bottomLines != nullptr) bottomLines->toFront(false);
    if (routingButton != nullptr) routingButton->toFront(false);
    if (oscButton != nullptr) oscButton->toFront(false);
    if (moduleAddButton != nullptr) moduleAddButton->toFront(false);
    if (hostButton != nullptr) hostButton->toFront(false);
    footerTab->toFront(false);

    if (routingPanel != nullptr && routingExpanded)
        routingPanel->toFront(false);
    if (oscPanel != nullptr && oscExpanded)
        oscPanel->toFront(false);

    if (textPromptOverlay != nullptr)
    {
        textPromptOverlay->setBounds(getLocalBounds());
        textPromptOverlay->toFront(false);
    }

    if (verticalResizeHandle != nullptr)
        verticalResizeHandle->toFront(false);
    if (horizontalResizeHandle != nullptr)
        horizontalResizeHandle->toFront(false);
    if (topResizeHandle != nullptr)
        topResizeHandle->toFront(false);
    if (leftResizeHandle != nullptr)
        leftResizeHandle->toFront(false);

    if (cornerResizeHandle != nullptr)
        cornerResizeHandle->toFront(false);

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
        || globalSpacer == nullptr
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
        || sortFiltersButton == nullptr
        || footerTab == nullptr)
        return;

    if (eqlModuleLoaded && addFilterButton == nullptr)
        return;

    for (const auto& section : filterSections)
    {
        if (eqlModuleLoaded && section == nullptr)
            return;
    }

    if (updateEditorResizeLimits()) return;
    auto bounds = svgExportLayoutActive ? svgExportLayoutBounds : getLocalBounds();

    if (verticalResizeHandle != nullptr)
        verticalResizeHandle->setBounds(bounds.withTop(juce::jmax(0, bounds.getBottom() - resizeHandleThickness)));
    if (horizontalResizeHandle != nullptr)
        horizontalResizeHandle->setBounds(bounds.withLeft(juce::jmax(0, bounds.getRight() - resizeHandleThickness)));

    if (topResizeHandle != nullptr)
        topResizeHandle->setBounds(bounds.withHeight(resizeHandleThickness));
    if (leftResizeHandle != nullptr)
        leftResizeHandle->setBounds(bounds.withWidth(resizeHandleThickness));

    if (cornerResizeHandle != nullptr)
        cornerResizeHandle->setBounds(bounds.getRight() - resizeHandleThickness,
            bounds.getBottom() - resizeHandleThickness, resizeHandleThickness, resizeHandleThickness);

    const auto editorInsetX = uiGap;
    const auto editorInsetTop = uiGap;
    const auto editorInsetBottom = uiGap;

    bounds.removeFromLeft(editorInsetX);
    bounds.removeFromRight(editorInsetX);

    bounds.removeFromBottom(editorInsetBottom);
    bounds.removeFromTop(editorInsetTop);

    layoutBottomControlsSection(bounds);
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
    if (hostParametersExpanded)
    {
        finalizeLayout();
        return;
    }
    layoutCrossoverSection(bounds);

    if (const auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
    {
        if (crossover->isCrossoverSettingsSelected())
        {
            finalizeLayout();
            return;
        }
    }

    if (! eqlModuleLoaded && ! tlsModuleLoaded && ! dynModuleLoaded)
    {
        finalizeLayout();
        return;
    }

    if (tlsModuleLoaded || dynModuleLoaded)
    {
        layoutModuleEditorContent(bounds);
        finalizeLayout();
        return;
    }

    layoutEqlModuleSections(bounds);

    finalizeLayout();
}

void AvaAudioProcessorEditor::layoutCrossoverSection(juce::Rectangle<int>& bounds)
{
    auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get());

    if (editor == nullptr)
        return;

    const auto sectionHeight = juce::jmin(bounds.getHeight(), editor->getPreferredHeight());
    editor->setBounds(bounds.removeFromTop(sectionHeight));
    editor->setVisible(sectionHeight > 0);
}

void AvaAudioProcessorEditor::layoutModuleEditorContent(juce::Rectangle<int>& bounds)
{
    auto contentBounds = bounds;

    if (tlsModuleEditor != nullptr)
    {
        tlsModuleEditor->setBounds(contentBounds);
        tlsModuleEditor->setVisible(tlsModuleLoaded);
    }

    if (dynModuleEditor != nullptr)
    {
        dynModuleEditor->setBounds(contentBounds.expanded(crossover_ui::dynControlGroupOutset, 0));
        dynModuleEditor->setVisible(dynModuleLoaded);
    }

}

bool AvaAudioProcessorEditor::updateEditorResizeLimits()
{
    if (auto* outer = dynamic_cast<AvaAudioProcessorEditor*>(getParentComponent());
        outer != nullptr && outer->activeInstanceEditor.get() == this)
    {
        outer->updateEditorResizeLimits();
        if (getBounds() != outer->getLocalBounds())
        {
            setBounds(outer->getLocalBounds());
            return true;
        }
    }
    const auto width = juce::jlimit(minimumEditorWidth, maximumEditorWidth, getWidth());
    const auto height = juce::jlimit(minimumEditorHeight, maximumEditorHeight, getHeight());
    setResizeLimits(minimumEditorWidth, minimumEditorHeight, maximumEditorWidth, maximumEditorHeight);
    if (width == getWidth() && height == getHeight()) return false;
    setSize(width, height);
    return true;
}

namespace
{
void collectSvgExportViewports(juce::Component& component, std::vector<juce::Viewport*>& found)
{
    if (auto* viewport = dynamic_cast<juce::Viewport*>(&component))
        found.push_back(viewport);

    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* child = component.getChildComponent(index))
            collectSvgExportViewports(*child, found);
}
}

void AvaAudioProcessorEditor::expandContentForSvgExport()
{
    svgExportFilterExpandedStates.clear();

    for (auto& section : filterSections)
    {
        if (section == nullptr)
            continue;

        svgExportFilterExpandedStates.push_back(section->expanded);
        section->expanded = true;
        section->disclosure.snap(true);
        section->disclosureInitialised = true;
    }

    if (eqlModuleLoaded && ! svgExportFilterExpandedStates.empty())
        updateEqlSectionStates(getActiveFilterCount());
}

void AvaAudioProcessorEditor::restoreContentAfterSvgExport()
{
    auto state = svgExportFilterExpandedStates.begin();

    for (auto& section : filterSections)
    {
        if (section == nullptr || state == svgExportFilterExpandedStates.end())
            continue;

        section->expanded = *state;
        section->disclosure.snap(*state);
        section->disclosureInitialised = true;
        ++state;
    }

    const auto sectionsWereExpanded = ! svgExportFilterExpandedStates.empty();
    svgExportFilterExpandedStates.clear();

    if (eqlModuleLoaded && sectionsWereExpanded)
        updateEqlSectionStates(getActiveFilterCount());
}

int AvaAudioProcessorEditor::getSvgExportCanvasHeight() const
{
    // Mirrors the branches of resized() so that exactly the content that is on
    // screen decides how much extra height the artwork needs.
    if (routingExpanded || oscExpanded)
        return 0;

    const auto addModuleOverflow = [](int& extraHeight, const juce::Component* component)
    {
        const auto* module = dynamic_cast<const CrossoverModuleComponent*>(component);

        if (module == nullptr || ! module->isVisible())
            return;

        extraHeight += juce::jmax(0, module->getPreferredHeight() - module->getHeight());
    };

    auto extraHeight = 0;

    if (hostParametersExpanded)
    {
        extraHeight += juce::jmax(0, hostParametersContent.getHeight() - hostParametersViewport.getHeight());
        return getHeight() + extraHeight;
    }

    addModuleOverflow(extraHeight, crossoverEditor.get());

    if (const auto* crossover = dynamic_cast<const CrossoverModuleComponent*>(crossoverEditor.get());
        crossover != nullptr && crossover->isCrossoverSettingsSelected())
        return getHeight() + extraHeight;

    if (! eqlModuleLoaded && ! tlsModuleLoaded && ! dynModuleLoaded)
        return getHeight() + extraHeight;

    if (tlsModuleLoaded || dynModuleLoaded)
    {
        addModuleOverflow(extraHeight, tlsModuleEditor.get());
        addModuleOverflow(extraHeight, dynModuleEditor.get());
        return getHeight() + extraHeight;
    }

    if (eqlModuleLoaded && filterViewport.isVisible())
        extraHeight += juce::jmax(0, getFlatFilterContentHeight() - filterViewport.getHeight());

    return getHeight() + extraHeight;
}

void AvaAudioProcessorEditor::prepareSvgExportLayout(const int canvasHeight)
{
    svgExportLayoutBounds = getLocalBounds().withHeight(juce::jmax(getHeight(), canvasHeight));
    svgExportLayoutActive = true;

    svgExportViewports.clear();
    svgExportViewportPositions.clear();
    collectSvgExportViewports(*this, svgExportViewports);

    for (const auto* viewport : svgExportViewports)
        svgExportViewportPositions.push_back(viewport->getViewPosition());

    // The taller layout is temporary: it must not overwrite the stored editor state.
    const juce::ScopedValueSetter<bool> stateGuard(restoringEditorUiState, true);
    resized();
}

void AvaAudioProcessorEditor::finishSvgExportLayout()
{
    svgExportLayoutActive = false;
    svgExportLayoutBounds = {};

    {
        const juce::ScopedValueSetter<bool> stateGuard(restoringEditorUiState, true);
        resized();
    }

    for (size_t index = 0; index < svgExportViewports.size() && index < svgExportViewportPositions.size(); ++index)
        svgExportViewports[index]->setViewPosition(svgExportViewportPositions[index]);

    svgExportViewports.clear();
    svgExportViewportPositions.clear();
}
