#include "ChoiceControl.h"
#include "Editor.h"
#include "FilterSection.h"
#include "Metrics.h"
#include "PresetSection.h"

#include <array>

void AvaAudioProcessorEditor::layoutEqlModuleSections(juce::Rectangle<int>& bounds)
{
    const auto placeButtonRow = [&bounds](const auto& buttons)
    {
        auto rowBounds = bounds.removeFromTop(rowHeight);
        const auto count = static_cast<int>(buttons.size());
        const auto available = juce::jmax(0, rowBounds.getWidth() - parameterGap * (count - 1));
        const auto extraDefaultWidth = count == 4 ? juce::jmin(12, available / 8) : 0;
        const auto width = (available - extraDefaultWidth) / count;
        for (int index = 0; index < count; ++index)
        {
            buttons[static_cast<size_t>(index)]->setBounds(rowBounds.removeFromLeft(index + 1 == count ? rowBounds.getWidth()
                : width + (buttons[static_cast<size_t>(index)]->getButtonText() == "DEFAULT" ? extraDefaultWidth : 0)));
            if (index + 1 < count)
                rowBounds.removeFromLeft(juce::jmin(parameterGap, rowBounds.getWidth()));
        }
        bounds.removeFromTop(juce::jmin(verticalGap, bounds.getHeight()));
    };
    if (presetsSection != nullptr)
    {
        auto presetBounds = bounds.removeFromTop(rowHeight);
        bounds.removeFromTop(juce::jmin(verticalGap, bounds.getHeight()));
        presetsSection->actionsToggleButton->setBounds(presetBounds.removeFromRight(iconControlSize));
        presetBounds.removeFromRight(juce::jmin(parameterGap, presetBounds.getWidth()));
        const auto actionsWidth = juce::roundToInt(presetBounds.getWidth() * presetsSection->actionsReveal.value());
        const auto coveredWidth = presetBounds.getWidth() - actionsWidth;
        presetsSection->actionsClip.setBounds(presetBounds.withTrimmedLeft(coveredWidth));
        presetsSection->presetClip.setBounds(presetBounds.withWidth(coveredWidth));
        auto actionBounds = presetBounds.withPosition(0, 0);
        auto presetContentBounds = presetBounds.withPosition(-actionsWidth, 0);
        const std::array<BoxTextButton*, 4> actions { presetsSection->saveButton.get(),
            presetsSection->renameButton.get(), presetsSection->defaultButton.get(), presetsSection->deleteButton.get() };
        const auto available = juce::jmax(0, actionBounds.getWidth() - 3 * parameterGap);
        const auto font = makeUiFont();
        std::array<int, 4> actionWidths {};
        auto totalTextWidth = 0;
        for (int index = 0; index < 4; ++index)
        {
            actionWidths[static_cast<size_t>(index)] = juce::GlyphArrangement::getStringWidthInt(
                font, actions[static_cast<size_t>(index)]->getButtonText());
            totalTextWidth += actionWidths[static_cast<size_t>(index)];
        }
        const auto spareWidth = juce::jmax(0, available - totalTextWidth);
        const auto commonPadding = spareWidth / 8;
        for (int index = 0; index < 4; ++index)
        {
            actionWidths[static_cast<size_t>(index)] += spareWidth / 4 + (index < spareWidth % 4 ? 1 : 0);
            actions[static_cast<size_t>(index)]->setTextHorizontalPadding(commonPadding);
        }
        for (int index = 0; index < 4; ++index)
        {
            actions[static_cast<size_t>(index)]->setBounds(actionBounds.removeFromLeft(index == 3
                ? actionBounds.getWidth() : actionWidths[static_cast<size_t>(index)]));
            if (index < 3)
                actionBounds.removeFromLeft(juce::jmin(parameterGap, actionBounds.getWidth()));
        }
        presetsSection->addButton->setBounds(presetContentBounds.removeFromRight(presetsSection->deleteButton->getWidth()));
        presetContentBounds.removeFromRight(juce::jmin(parameterGap, presetContentBounds.getWidth()));
        presetsSection->presetCombo.setBounds(presetContentBounds);
    }
    placeButtonRow(std::array<BoxTextButton*, 2> { addFilterButton.get(), sortFiltersButton.get() });

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

        const auto frameTop = contentBounds.getY();
        const auto headerTopInset = (section->visibleBodyHeight() > 0 ? FilterSection::contentInset : 0);
        auto headerBounds = contentBounds.removeFromTop(rowHeight);

        auto* orderLabel = filterOrderLabels[static_cast<size_t>(displayIndex)].get();
        auto orderLabelBounds = headerBounds.removeFromLeft(48);
        orderLabelBounds.setY(frameTop);
        headerBounds.removeFromLeft(parameterGap);

        auto bypassBounds = headerBounds.removeFromLeft(iconControlSize);
        bypassBounds.setY(frameTop);
        headerBounds.removeFromLeft(parameterGap);

        if (orderLabel != nullptr)
            orderLabel->setBounds(orderLabelBounds);
        section->bypassButton->setBounds(bypassBounds);
        section->header->setBounds(headerBounds.reduced(headerTopInset, 0).translated(0, headerTopInset));

        if (! contentBounds.isEmpty())
            contentBounds.removeFromTop(verticalGap);

        const auto bodyHeight = section->visibleBodyHeight();
        section->body.setBounds(contentBounds.removeFromTop(bodyHeight));
        if (bodyHeight == 0)
        {
            section->accentFrame.setVisible(false);
            continue;
        }

        auto bodyBounds = section->body.getLocalBounds();
        bodyBounds.removeFromTop(FilterSection::contentInset);
        auto placeFilterControl = [&bodyBounds] (auto& control)
        {
            control.setBounds(bodyBounds.removeFromTop(control.getPreferredHeight()).reduced(FilterSection::contentInset, 0));
            bodyBounds.removeFromTop(verticalGap);
        };
        placeFilterControl(*section->typeControl);
        placeFilterControl(*section->orderControl);
        placeFilterControl(*section->frequencyControl);
        placeFilterControl(*section->bandwidthControl);
        placeFilterControl(*section->gainControl);
        section->accentFrame.setBounds(0, frameTop, bounds.getWidth(),
            juce::jmax(rowHeight, section->body.getBottom() - verticalGap - frameTop));
        section->accentFrame.setContour(headerBounds.getX(), rowHeight + verticalGap);
        section->accentFrame.setVisible(true);
        section->accentFrame.toFront(false);
    }

    filterContent.setSize(bounds.getWidth(), juce::jmax(bounds.getHeight(), getFilterContentHeight()));

}
