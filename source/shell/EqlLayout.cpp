#include "Editor.h"
#include "FilterSection.h"
#include "UiConstants.h"
#include "PresetSections.h"

#include <array>

void AvaAudioProcessorEditor::layoutEqlModuleSections(juce::Rectangle<int>& bounds)
{
    if (! bounds.isEmpty())
        bounds.removeFromBottom(viewportToPotentiometerGap);

    juce::Rectangle<int> presetsBounds;

    if (presetsSection != nullptr)
    {
        const auto presetHeight = presetsSection->getPresetRowPreferredHeight();
        presetsBounds = bounds.removeFromBottom(juce::jmin(bounds.getHeight(), presetHeight));

        if (! bounds.isEmpty())
            bounds.removeFromBottom(verticalGap);
    }

    auto actionRowBounds = bounds.removeFromTop(rowHeight);

    std::array<BoxTextButton*, 4> actionButtons {
        addFilterButton.get(),
        sortPlaceButton.get(),
        sortFreqButton.get(),
        sortDuoButton.get()
    };

    auto remainingActionBounds = actionRowBounds;
    const auto actionButtonCount = static_cast<int>(actionButtons.size());
    const auto actionTotalGap = parameterGap * (actionButtonCount - 1);
    const auto actionBaseWidth = juce::jmax(0, (remainingActionBounds.getWidth() - actionTotalGap) / actionButtonCount);

    for (int buttonIndex = 0; buttonIndex < actionButtonCount; ++buttonIndex)
    {
        auto* button = actionButtons[static_cast<size_t>(buttonIndex)];

        if (button == nullptr)
            continue;

        const auto isLastButton = buttonIndex + 1 == actionButtonCount;
        auto buttonBounds = isLastButton ? remainingActionBounds
                                         : remainingActionBounds.removeFromLeft(actionBaseWidth);
        button->setBounds(buttonBounds);

        if (! isLastButton)
            remainingActionBounds.removeFromLeft(parameterGap);
    }

    if (! bounds.isEmpty())
        bounds.removeFromTop(globalToFilterGap);

    filterViewport.setBounds(bounds);
    filterViewport.setVisible(true);
    filterContent.setSize(bounds.getWidth(), juce::jmax(bounds.getHeight(), getFilterContentHeight()));

    auto contentBounds = filterContent.getLocalBounds();

    const auto activeFilterCount = getActiveFilterCount();

    for (int displayIndex = 0; displayIndex < activeFilterCount; ++displayIndex)
    {
        const auto filterIndex = getFilterIndexForOrderPosition(displayIndex);

        if (filterIndex < 0)
            continue;

        auto* section = filterSections[static_cast<size_t>(filterIndex)].get();

        if (section == nullptr)
            continue;

        auto headerBounds = contentBounds.removeFromTop(rowHeight);

        auto* orderLabel = filterOrderLabels[static_cast<size_t>(displayIndex)].get();
        auto orderLabelBounds = headerBounds.removeFromLeft(48);
        headerBounds.removeFromLeft(parameterGap);

        auto bypassBounds = headerBounds.removeFromLeft(45);
        headerBounds.removeFromLeft(parameterGap);

        if (orderLabel != nullptr)
            orderLabel->setBounds(orderLabelBounds);
        section->bypassButton->setBounds(bypassBounds);
        section->header->setBounds(headerBounds);

        if (! contentBounds.isEmpty())
            contentBounds.removeFromTop(verticalGap);

        if (! section->expanded)
            continue;

        auto placeFilterControl = [&contentBounds] (auto& control)
        {
            auto controlBounds = contentBounds.removeFromTop(control.getPreferredHeight());
            control.setBounds(controlBounds);

            if (! contentBounds.isEmpty())
                contentBounds.removeFromTop(verticalGap);
        };

        placeFilterControl(*section->typeControl);
        placeFilterControl(*section->placeControl);
        placeFilterControl(*section->orderControl);
        placeFilterControl(*section->frequencyControl);
        placeFilterControl(*section->bandwidthControl);
        placeFilterControl(*section->gainControl);
    }

    if (presetsSection != nullptr)
    {
        auto presetContentBounds = presetsBounds;

        auto presetNameRowBounds = presetContentBounds.removeFromTop(rowHeight);
        presetsSection->presetCombo.setBounds(presetNameRowBounds);

        if (! presetContentBounds.isEmpty())
            presetContentBounds.removeFromTop(verticalGap);

        auto presetButtonRowBounds = presetContentBounds.removeFromTop(rowHeight);

        const auto presetButtonCount = 5;
        const auto totalGapWidth = presetRowGap * (presetButtonCount - 1);
        const auto availableButtonWidth = juce::jmax(0, presetButtonRowBounds.getWidth() - totalGapWidth);
        const auto baseButtonWidth = availableButtonWidth / presetButtonCount;
        const auto buttonWidthRemainder = availableButtonWidth % presetButtonCount;

        auto placePresetButton = [&presetButtonRowBounds, baseButtonWidth, buttonWidthRemainder] (BoxTextButton& button, const int index)
        {
            const auto buttonWidth = baseButtonWidth + (index < buttonWidthRemainder ? 1 : 0);
            auto buttonBounds = presetButtonRowBounds.removeFromLeft(buttonWidth);
            button.setBounds(buttonBounds);

            if (index + 1 < presetButtonCount)
                presetButtonRowBounds.removeFromLeft(presetRowGap);
        };

        placePresetButton(*presetsSection->addButton, 0);
        placePresetButton(*presetsSection->saveButton, 1);
        placePresetButton(*presetsSection->renameButton, 2);
        placePresetButton(*presetsSection->defaultButton, 3);
        placePresetButton(*presetsSection->deleteButton, 4);
    }

}
