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
        auto presetRowBounds = presetsBounds.removeFromTop(rowHeight);
        auto leftToggleBounds = presetRowBounds.removeFromLeft(iconControlSize);
        presetRowBounds.removeFromLeft(juce::jmin(parameterGap, presetRowBounds.getWidth()));
        auto rightToggleBounds = presetRowBounds.removeFromRight(iconControlSize);
        presetRowBounds.removeFromRight(juce::jmin(parameterGap, presetRowBounds.getWidth()));
        presetsSection->filterActionsToggleButton->setBounds(leftToggleBounds);
        presetsSection->actionsToggleButton->setBounds(rightToggleBounds);

        auto placeButtons = [] (juce::Rectangle<int> rowBounds, const auto& buttons)
        {
            const auto count = static_cast<int>(buttons.size());
            const auto totalGapWidth = presetRowGap * (count - 1);
            const auto availableButtonWidth = juce::jmax(0, rowBounds.getWidth() - totalGapWidth);
            const auto baseButtonWidth = availableButtonWidth / count;
            const auto buttonWidthRemainder = availableButtonWidth % count;

            for (int index = 0; index < count; ++index)
            {
                const auto buttonWidth = baseButtonWidth + (index < buttonWidthRemainder ? 1 : 0);
                buttons[static_cast<size_t>(index)]->setBounds(rowBounds.removeFromLeft(buttonWidth));

                if (index + 1 < count)
                    rowBounds.removeFromLeft(presetRowGap);
            }
        };

        if (presetsSection->filterActionsExpanded)
        {
            presetsSection->presetCombo.setBounds({});
            placeButtons(presetRowBounds, std::array<BoxTextButton*, 4> {
                addFilterButton.get(), sortPlaceButton.get(), sortFreqButton.get(), sortDuoButton.get()
            });
        }
        else
        {
            for (auto* button : { addFilterButton.get(), sortPlaceButton.get(), sortFreqButton.get(), sortDuoButton.get() })
                button->setBounds({});
        }

        if (presetsSection->actionsExpanded)
        {
            presetsSection->presetCombo.setBounds({});
            placeButtons(presetRowBounds, std::array<BoxTextButton*, 5> {
                presetsSection->addButton.get(), presetsSection->saveButton.get(),
                presetsSection->renameButton.get(), presetsSection->defaultButton.get(),
                presetsSection->deleteButton.get()
            });
        }
        else
        {
            if (! presetsSection->filterActionsExpanded)
                presetsSection->presetCombo.setBounds(presetRowBounds);
            presetsSection->addButton->setBounds({});
            presetsSection->saveButton->setBounds({});
            presetsSection->renameButton->setBounds({});
            presetsSection->defaultButton->setBounds({});
            presetsSection->deleteButton->setBounds({});
        }
    }

}
