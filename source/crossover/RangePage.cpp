#include "RangePage.h"
#include "UiSupport.h"

#include <algorithm>
#include <cstddef>

using namespace crossover_ui;
using ControlKind = CrossoverModuleComponent::ControlKind;

namespace
{
template <typename Rows>
int getRowsHeight(const Rows& rows, const size_t firstRow, const size_t lastRow)
{
    auto height = 0;

    for (size_t index = firstRow; index < lastRow;)
    {
        const auto& row = rows[index];
        const auto controlsInRow = juce::jlimit<size_t>(1,
                                                        lastRow - index,
                                                        static_cast<size_t>(juce::jmax(1, row->controlsInRow)));
        auto topGap = 0;
        auto preferredHeight = 0;

        for (size_t offset = 0; offset < controlsInRow; ++offset)
        {
            topGap = juce::jmax(topGap, rows[index + offset]->getTopGap());
            preferredHeight = juce::jmax(preferredHeight, rows[index + offset]->getPreferredHeight());
        }

        height += topGap + preferredHeight;
        index += controlsInRow;
    }

    return height;
}

template <typename Rows>
void layoutRows(Rows& rows,
                const size_t firstRow,
                const size_t lastRow,
                juce::Rectangle<int> rowBounds,
                const int parameterGap)
{
    for (size_t index = firstRow; index < lastRow;)
    {
        auto& row = rows[index];
        const auto controlsInRow = juce::jlimit<size_t>(1,
                                                        lastRow - index,
                                                        static_cast<size_t>(juce::jmax(1, row->controlsInRow)));
        auto topGap = 0;
        auto preferredHeight = 0;

        for (size_t offset = 0; offset < controlsInRow; ++offset)
        {
            topGap = juce::jmax(topGap, rows[index + offset]->getTopGap());
            preferredHeight = juce::jmax(preferredHeight, rows[index + offset]->getPreferredHeight());
        }

        if (! rowBounds.isEmpty())
            rowBounds.removeFromTop(juce::jmin(topGap, rowBounds.getHeight()));

        auto controlBounds = rowBounds.removeFromTop(preferredHeight);
        const auto availableWidth = juce::jmax(0,
                                               controlBounds.getWidth()
                                                   - (parameterGap * static_cast<int>(controlsInRow - 1)));
        const auto controlWidth = availableWidth / static_cast<int>(controlsInRow);

        for (size_t offset = 0; offset < controlsInRow; ++offset)
        {
            const auto isLast = offset + 1 == controlsInRow;
            rows[index + offset]->setBounds(controlBounds.removeFromLeft(isLast ? controlBounds.getWidth()
                                                                                : controlWidth));

            if (! isLast)
                controlBounds.removeFromLeft(juce::jmin(parameterGap, controlBounds.getWidth()));
        }

        index += controlsInRow;
    }
}
}

CrossoverRangePage::CrossoverRangePage(CrossoverModuleComponent& ownerIn,
                                       const size_t rangeIndexIn,
                                       juce::Colour accent)
    : owner(ownerIn),
      rangeIndex(rangeIndexIn),
      soloButton(accent),
      moduleHeading(uiGrey500)
{
    soloButton.setButtonText("SOLO");
    soloButton.setTextJustification(juce::Justification::centred);
    soloButton.setClickingTogglesState(false);
    soloButton.setLongPressPromptActions({}, [this]
    {
        if (owner.config.makeCrossoverSoloParameterId != nullptr)
            owner.assignButtonToHostSlot(owner.config.makeCrossoverSoloParameterId(rangeIndex), "SOLO", &soloButton);
    });
    soloButton.onClick = [this]
    {
        owner.toggleManualSolo(rangeIndex);
        refreshSoloButtonState();
    };
    if (owner.config.showCrossoverSolo)
        addAndMakeVisible(soloButton);

    if (owner.config.showModuleHeading)
    {
        moduleHeading.setButtonText(owner.config.moduleKey.toUpperCase());
        moduleHeading.setTextJustification(juce::Justification::centred);
        moduleHeading.setAlwaysAccentOutline(false);
        moduleHeading.setToggleAccentVisible(false);
        moduleHeading.setLongPressAction([this]
        {
            if (owner.config.onModuleCloseRequest != nullptr)
                owner.config.onModuleCloseRequest();
        }, 500, "CLOSE?");
        addAndMakeVisible(moduleHeading);
    }

    if (owner.config.showCrossoverSolo)
    {
        const auto soloParameterId = owner.config.makeCrossoverSoloParameterId(rangeIndex);
        listenedParameterIds.push_back(soloParameterId);
        owner.valueTreeState.addParameterListener(soloParameterId, this);
    }

    pinnedTail = std::make_unique<juce::Component>();
    addControlSpecs(owner.config.rangeControls, *this);
    tailRowStart = rows.size();
    addControlSpecs(owner.config.rangeTailControls, *pinnedTail);
    refreshSoloButtonState();
    updateTimeModeControls();
}

CrossoverRangePage::~CrossoverRangePage()
{
    for (const auto& parameterId : listenedParameterIds)
        owner.valueTreeState.removeParameterListener(parameterId, this);
}

void CrossoverRangePage::refreshExternalState()
{
    const auto previousPreferredHeight = getPreferredHeight();
    const auto orderChanged = reorderRows("gain");
    refreshSoloButtonState();
    updateToggleLabels();
    updateTimeModeControls();

    if (orderChanged || previousPreferredHeight != getPreferredHeight())
        resized();
}

int CrossoverRangePage::getPreferredHeight() const
{
    auto height = 0;
    const auto moduleHeadingIsPinned = owner.config.showModuleHeading && owner.config.pinModuleHeading;

    if (owner.config.showCrossoverSolo)
        height += rowHeight;
    if (owner.config.showModuleHeading && ! moduleHeadingIsPinned)
        height += (height > 0 ? verticalGap : 0) + rowHeight;

    height += getRowsHeight(rows, 0, tailRowStart);

    return height + ((! moduleHeadingIsPinned && owner.config.showModuleHeading) || ! rows.empty()
                         ? moduleContentBottomGap
                         : 0);
}

juce::Component* CrossoverRangePage::getPinnedHeaderComponent() noexcept
{
    return owner.config.showModuleHeading && owner.config.pinModuleHeading ? &moduleHeading : nullptr;
}

int CrossoverRangePage::getPinnedHeaderHeight() const noexcept
{
    return owner.config.showModuleHeading && owner.config.pinModuleHeading ? rowHeight : 0;
}

juce::Component* CrossoverRangePage::getPinnedTailComponent() noexcept
{
    return pinnedTail.get();
}

int CrossoverRangePage::getPinnedTailHeight() const noexcept
{
    return getRowsHeight(rows, tailRowStart, rows.size());
}

void CrossoverRangePage::layoutPinnedTail()
{
    if (pinnedTail == nullptr)
        return;

    layoutRows(rows,
                                             tailRowStart,
                                             rows.size(),
                                             pinnedTail->getLocalBounds(),
                                             parameterGap);
}

void CrossoverRangePage::resized()
{
    auto bounds = getLocalBounds();
    if (owner.config.showCrossoverSolo)
        soloButton.setBounds(bounds.removeFromTop(rowHeight));

    if (owner.config.showModuleHeading && ! owner.config.pinModuleHeading)
    {
        if (owner.config.showCrossoverSolo && ! bounds.isEmpty())
            bounds.removeFromTop(verticalGap);

        moduleHeading.setBounds(bounds.removeFromTop(rowHeight));
    }

    layoutRows(rows,
                                             0,
                                             tailRowStart,
                                             bounds,
                                             parameterGap);
}

void CrossoverRangePage::mouseDown(const juce::MouseEvent&)
{
    owner.clearFocus();
}

void CrossoverRangePage::refreshSoloButtonState()
{
    const auto enabled = ! owner.autoSoloEnabled && owner.getActiveRangeCount() > 1;
    soloButton.setEnabled(enabled);
    soloButton.setAlpha(1.0f);
    soloButton.setToggleState(enabled && owner.isRangeSoloEnabled(rangeIndex), juce::dontSendNotification);
}

void CrossoverRangePage::updateToggleLabels()
{
    for (auto& row : rows)
        row->refreshExternalState();
}

void CrossoverRangePage::updateTimeModeControls()
{
    for (auto& row : rows)
        row->refreshExternalState();
}

void CrossoverRangePage::parameterChanged(const juce::String& parameterID, float)
{
    if (std::find(listenedParameterIds.begin(), listenedParameterIds.end(), parameterID) == listenedParameterIds.end())
        return;

    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<CrossoverRangePage>(this)]
    {
        if (safeThis != nullptr)
        {
            safeThis->owner.synchroniseManualSoloMaskFromParameters();
            safeThis->refreshExternalState();
        }
    });
}

std::unique_ptr<CrossoverModulePage> makeCrossoverRangePage(CrossoverModuleComponent& owner,
                                                            const size_t rangeIndex,
                                                            const juce::Colour accent)
{
    return std::make_unique<CrossoverRangePage>(owner, rangeIndex, accent);
}

juce::String CrossoverRangePage::getCrossoverRangeParameterId(const CrossoverControlSpec& spec) const
{
    const auto sourceRange = spec.sourceRangeIndex >= 0
        ? static_cast<size_t>(juce::jlimit(0, static_cast<int>(CrossoverModuleComponent::numRanges - 1), spec.sourceRangeIndex))
        : rangeIndex;
    return owner.config.makeRangeParameterId(sourceRange, spec.suffix);
}

void CrossoverRangePage::addControlSpecs(const std::vector<CrossoverControlSpec>& specs, juce::Component& parent)
{
    for (const auto& spec : specs)
    {
        if (spec.kind == ControlKind::heading)
        {
            auto row = std::make_unique<HeadingRow>(spec);
            row->controlsInRow = spec.controlsInRow;
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::toggle)
        {
            auto row = std::make_unique<ToggleRow>(*this, owner, getCrossoverRangeParameterId(spec), spec);
            row->controlsInRow = spec.controlsInRow;
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::choice)
        {
            auto row = std::make_unique<ChoiceRow>(owner, getCrossoverRangeParameterId(spec), spec);
            row->controlsInRow = spec.controlsInRow;
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::inactive)
        {
            auto row = std::make_unique<InactiveRow>(spec);
            row->controlsInRow = spec.controlsInRow;
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::time)
        {
            const auto valueId = getCrossoverRangeParameterId(spec);
            const auto modeId = owner.config.makeRangeParameterId(rangeIndex, spec.modeSuffix);
            const auto syncId = owner.config.makeRangeParameterId(rangeIndex, spec.syncSuffix);
            auto row = std::make_unique<TimeRow>(owner, valueId, modeId, syncId, spec);
            row->controlsInRow = spec.controlsInRow;
            listenedParameterIds.push_back(modeId);
            listenedParameterIds.push_back(syncId);
            owner.valueTreeState.addParameterListener(modeId, this);
            owner.valueTreeState.addParameterListener(syncId, this);
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::readout)
        {
            const auto degreeId = getCrossoverRangeParameterId(spec);
            const auto flipId = owner.config.makeRangeParameterId(rangeIndex, spec.modeSuffix);
            auto row = std::make_unique<ReadoutRow>(owner, degreeId, flipId, spec);
            row->controlsInRow = spec.controlsInRow;
            listenedParameterIds.push_back(degreeId);
            listenedParameterIds.push_back(flipId);
            owner.valueTreeState.addParameterListener(degreeId, this);
            owner.valueTreeState.addParameterListener(flipId, this);
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        const auto auxiliaryToggleId = spec.auxiliaryToggleSuffix != nullptr
                                           && juce::String(spec.auxiliaryToggleSuffix).isNotEmpty()
            ? owner.config.makeRangeParameterId(rangeIndex, spec.auxiliaryToggleSuffix)
            : juce::String {};
        const auto sourceRange = spec.sourceRangeIndex >= 0
            ? static_cast<size_t>(juce::jlimit(0, static_cast<int>(CrossoverModuleComponent::numRanges - 1), spec.sourceRangeIndex))
            : rangeIndex;
        const auto enabledWhenId = spec.enabledWhenSuffix != nullptr
                                        && juce::String(spec.enabledWhenSuffix).isNotEmpty()
            ? owner.config.makeRangeParameterId(sourceRange, spec.enabledWhenSuffix)
            : juce::String {};
        auto row = std::make_unique<ParameterRow>(*this,
                                                  owner,
                                                  getCrossoverRangeParameterId(spec),
                                                  auxiliaryToggleId,
                                                  enabledWhenId,
                                                  spec);
        row->controlsInRow = spec.controlsInRow;

        if (auxiliaryToggleId.isNotEmpty() && spec.auxiliaryToggleInverted)
        {
            listenedParameterIds.push_back(auxiliaryToggleId);
            owner.valueTreeState.addParameterListener(auxiliaryToggleId, this);
        }

        if (enabledWhenId.isNotEmpty())
        {
            listenedParameterIds.push_back(enabledWhenId);
            owner.valueTreeState.addParameterListener(enabledWhenId, this);
        }

        if (row->orderParameterId.isNotEmpty())
        {
            listenedParameterIds.push_back(row->orderParameterId);
            owner.valueTreeState.addParameterListener(row->orderParameterId, this);
        }

        parent.addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

    reorderRows("gain");
}

void CrossoverRangePage::moveReorderRow(ParameterRow& sourceRow, const int delta)
{
    if (sourceRow.fixedOrder || sourceRow.orderParameterId.isEmpty() || delta == 0)
        return;

    const auto sourceOrder = juce::roundToInt(
        readRawParameter(owner.valueTreeState, sourceRow.orderParameterId, 0.0f));
    const auto destinationOrder = sourceOrder + delta;

    if (! juce::isPositiveAndBelow(destinationOrder, 4))
        return;

    for (auto& row : rows)
    {
        if (row.get() == &sourceRow
            || row->reorderGroup != sourceRow.reorderGroup
            || row->orderParameterId.isEmpty())
        {
            continue;
        }

        if (juce::roundToInt(readRawParameter(owner.valueTreeState,
                                              row->orderParameterId,
                                              -1.0f)) == destinationOrder)
        {
            if (owner.swapParameterPlainValues(sourceRow.orderParameterId,
                                               row->orderParameterId))
            {
                reorderRows(sourceRow.reorderGroup);
                refreshExternalState();
                owner.refreshCurrentPageLayout();
            }

            return;
        }
    }
}

void CrossoverRangePage::armReorderMove(ParameterRow& sourceRow)
{
    if (sourceRow.fixedOrder || sourceRow.orderParameterId.isEmpty())
        return;

    reorderMoveSource = &sourceRow;

    for (const auto& row : rows)
    {
        auto* parameterRow = dynamic_cast<ParameterRow*>(row.get());

        if (parameterRow != nullptr
            && parameterRow->reorderGroup == sourceRow.reorderGroup
            && parameterRow->orderLabel != nullptr)
        {
            parameterRow->orderLabel->setDragTargetOutlineVisible(parameterRow == &sourceRow);
        }
    }
}

void CrossoverRangePage::applyReorderMove(ParameterRow& destinationRow)
{
    auto* sourceRow = reorderMoveSource;
    reorderMoveSource = nullptr;

    const auto group = sourceRow != nullptr ? sourceRow->reorderGroup
                                            : destinationRow.reorderGroup;
    clearReorderDragTarget(group);

    if (sourceRow == nullptr
        || sourceRow == &destinationRow
        || sourceRow->fixedOrder
        || destinationRow.fixedOrder
        || sourceRow->orderParameterId.isEmpty()
        || destinationRow.orderParameterId.isEmpty()
        || sourceRow->reorderGroup != destinationRow.reorderGroup)
    {
        return;
    }

    const auto destinationOrder = juce::roundToInt(
        readRawParameter(owner.valueTreeState, destinationRow.orderParameterId, 0.0f));

    for (auto moveCount = 0; moveCount < 4; ++moveCount)
    {
        const auto sourceOrder = juce::roundToInt(
            readRawParameter(owner.valueTreeState, sourceRow->orderParameterId, 0.0f));

        if (sourceOrder == destinationOrder)
        {
            if (sourceRow->orderLabel != nullptr)
                sourceRow->orderLabel->flashConfirmationOutline();

            owner.clearFocus();
            return;
        }

        moveReorderRow(*sourceRow, destinationOrder > sourceOrder ? 1 : -1);
    }
}

void CrossoverRangePage::clearReorderDragTarget(const juce::String& group)
{
    for (const auto& row : rows)
    {
        auto* parameterRow = dynamic_cast<ParameterRow*>(row.get());

        if (parameterRow != nullptr
            && parameterRow->reorderGroup == group
            && parameterRow->orderLabel != nullptr)
        {
            parameterRow->orderLabel->setDragTargetOutlineVisible(false);
        }
    }
}

bool CrossoverRangePage::reorderRows(const juce::String& group)
{
    auto first = std::find_if(rows.begin(), rows.end(), [&group] (const auto& row)
    {
        return row->reorderGroup == group;
    });

    if (first == rows.end())
        return false;

    auto last = first;

    while (last != rows.end() && (*last)->reorderGroup == group)
        ++last;

    std::vector<RowBase*> previousOrder;
    previousOrder.reserve(static_cast<size_t>(std::distance(first, last)));

    for (auto current = first; current != last; ++current)
        previousOrder.push_back(current->get());

    std::stable_sort(first, last, [this] (const auto& firstRow, const auto& secondRow)
    {
        const auto getOrder = [this] (const auto& row)
        {
            return row->fixedOrder
                ? -1
                : juce::roundToInt(readRawParameter(owner.valueTreeState,
                                                    row->orderParameterId,
                                                    0.0f));
        };

        return getOrder(firstRow) < getOrder(secondRow);
    });

    return ! std::equal(first,
                        last,
                        previousOrder.begin(),
                        [] (const auto& row, const auto* previous) { return row.get() == previous; });
}

void CrossoverRangePage::clearExclusiveToggleGroup(const juce::String& activeParameterId,
                                                    const juce::String& exclusiveGroup)
{
    if (exclusiveGroup.isEmpty())
        return;

    for (auto& row : rows)
    {
        auto* toggleRow = dynamic_cast<ToggleRow*>(row.get());

        if (toggleRow == nullptr
            || toggleRow->exclusiveGroup != exclusiveGroup
            || toggleRow->parameterIdToToggle == activeParameterId)
        {
            continue;
        }

        owner.setParameterPlainValue(toggleRow->parameterIdToToggle, 0.0f);
    }
}
