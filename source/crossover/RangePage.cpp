#include "RangePage.h"
#include "UiSupport.h"
#include "../shell/EditorSetup.h"
#include "../shell/Editor.h"
#include "../shell/EditorStateSchema.h"

#include <algorithm>
#include <cstddef>

using namespace crossover_ui;
using ControlKind = CrossoverModuleComponent::ControlKind;

namespace
{
template <typename Rows>
size_t fieldsInRow(const Rows& rows, size_t index, size_t last)
{
    if (!rows[index]->isValueField())
        return juce::jlimit<size_t>(1, last - index, size_t(juce::jmax(1, rows[index]->controlsInRow)));
    size_t count = 1;
    while (index + count < last)
    {
        const auto& next = *rows[index + count];
        if (next.layoutHidden || !next.isValueField() || next.blockIndex != rows[index]->blockIndex
            || bool(next.getProperties().getWithDefault("startNewControlRow", false))) break;
        ++count;
    }
    return count;
}

template <typename Rows, typename Place>
juce::Point<int> visitRows(const Rows& rows, size_t first, size_t last, int availableWidth,
                           int columns, Place&& place)
{
    int y = 0, widest = 0;
    bool firstVisible = first == 0;
    for (auto index = first; index < last;)
    {
        if (rows[index]->layoutHidden) { ++index; continue; }
        auto count = fieldsInRow(rows, index, last);
        if (columns > 0 && rows[index]->isValueField()) count = juce::jmin(count, size_t(columns));
        if (availableWidth > 0 && rows[index]->isValueField())
        {
            int used = 0;
            size_t fitting = 0;
            while (fitting < count)
            {
                const auto width = rows[index + fitting]->getPreferredWidth();
                const auto next = used + (fitting > 0 ? getUiGap(*rows[index + fitting]) : 0) + width;
                if (fitting > 0 && next > availableWidth) break;
                used = next;
                ++fitting;
            }
            count = fitting;
        }
        int topGap = 0, height = 0;
        for (size_t offset = 0; offset < count; ++offset)
        {
            topGap = juce::jmax(topGap, rows[index + offset]->getTopGap());
            height = juce::jmax(height, rows[index + offset]->getLayoutHeight(availableWidth));
        }
        if (firstVisible) { topGap = 0; firstVisible = false; }
        else if (rows[index]->isValueField()) topGap = getUiRowGap(*rows[index]);
        y += topGap;
        int x = 0;
        for (size_t offset = 0; offset < count; ++offset)
        {
            auto& field = *rows[index + offset];
            const auto width = field.getPreferredWidth();
            place(field, juce::Rectangle<int>(x, y,
                field.isValueField() ? juce::jmin(width, availableWidth > 0 ? availableWidth : width)
                    : (availableWidth > 0 ? availableWidth : width), field.getLayoutHeight(availableWidth)));
            widest = juce::jmax(widest, x + width);
            x += width + getUiGap(field);
        }
        y += height;
        index += count;
    }
    return { widest, y };
}

template <typename Rows>
juce::Point<int> measureRows(const Rows& rows, int columns = 0, int availableWidth = 0)
{
    return visitRows(rows, 0, rows.size(), availableWidth, columns, [](auto&, auto) {});
}

template <typename Rows>
void layoutRows(Rows& rows, size_t first, size_t last, juce::Rectangle<int> bounds, int columns = 0)
{
    visitRows(rows, first, last, bounds.getWidth(), columns, [&bounds](auto& field, auto cell)
    {
        field.setBounds(cell.translated(bounds.getX(), bounds.getY()));
    });
}

}

CrossoverRangePage::ParameterGroup::ParameterGroup(juce::String name,
    std::vector<std::unique_ptr<RowBase>> membersIn)
    : members(std::move(membersIn))
{
    setName("group-" + name);
    label = name.replace("exclusive-", "").replace("-", " ").toUpperCase();
    if (label == "TRANS") label = "TRANS SUST";
    hasTopGap = members.front()->hasTopGap;
    members.front()->hasTopGap = false;
    for (auto& member : members) addAndMakeVisible(*member);

}

int CrossoverRangePage::ParameterGroup::getPreferredHeight() const
{
    return getLayoutHeight(getWidth());
}

int CrossoverRangePage::ParameterGroup::getLayoutHeight(int width) const
{
    const auto padding = bool(getProperties().getWithDefault("independentBlock", false)) ? controlBlockPadding + controlBlockFrameThickness : controlBlockPadding;
    if (isBlockCollapsed(label))
        return controlBlockHeaderHeight
            + (bool(getProperties().getWithDefault("independentBlock", false))
                ? 0 : 2 * controlBlockFrameGap);
    const auto rowsHeight = measureRows(members, columns,
        width > 0 ? juce::jmax(1, width - 2 * padding) : 0).y;
    if (bool(getProperties().getWithDefault("independentBlock", false)))
        return controlBlockHeaderHeight + uiGap + rowsHeight + uiGap + controlBlockFrameThickness;
    return rowsHeight + 2 * padding + controlBlockHeaderHeight;
}

int CrossoverRangePage::ParameterGroup::getPreferredWidth() const
{
    // LISTEN uses six grid columns even though only four controls occupy
    // each row. Reserve the blank middle columns in its preferred width.
    if (blockIndex == int(ava::modules::chain::blocks::Id::listen))
        return 6 * getSquareControlSize(*this) + 5 * getUiGap(*this)
            + 2 * controlBlockPadding;
    return measureRows(members, columns).x + 2 * controlBlockPadding;
}

int CrossoverRangePage::ParameterGroup::getTopGap() const noexcept
{
    return 0;
}

void CrossoverRangePage::ParameterGroup::refreshExternalState()
{
    for (auto& member : members) member->refreshExternalState();
}

void CrossoverRangePage::ParameterGroup::resized()
{
    const auto independent = bool(getProperties().getWithDefault("independentBlock", false));
    const auto padding = independent ? controlBlockPadding + controlBlockFrameThickness : controlBlockPadding;
    const auto collapsed = isBlockCollapsed(label);
    juce::Rectangle<int> occupied;
    for (const auto& member : members)
    {
        member->setVisible(!collapsed && !member->layoutHidden);
        if (collapsed) member->setBounds({});
    }
    if (!collapsed)
    {
        auto controlsBounds = independent
            ? getLocalBounds().withTrimmedLeft(padding).withTrimmedRight(padding)
                .withTrimmedTop(controlBlockHeaderHeight + uiGap)
                .withTrimmedBottom(uiGap + controlBlockFrameThickness)
            : getLocalBounds().reduced(padding).withTrimmedTop(controlBlockHeaderHeight);
        layoutRows(members, 0, members.size(), controlsBounds, columns);

        // LISTEN: LC / RC and MC / SC stay in columns 1-2; the other
        // two controls in each row belong at the *right end* of the grid.
        // Retain four logical controls per row for height calculation,
        // then position LL / RR and MS+3 / SS in the last two columns.
        if (blockIndex == int(ava::modules::chain::blocks::Id::listen)
            && members.size() == 8)
        {
            const auto size = getSquareControlSize(*this);
            const auto gap = getUiGap(*this);
            const auto stride = size + gap;
            const auto availableColumns = (controlsBounds.getWidth() + gap) / stride;
            if (availableColumns >= 6)
            {
                const auto rightPairX = controlsBounds.getX() + (availableColumns - 2) * stride;
                for (size_t row = 0; row < 2; ++row)
                    for (size_t offset = 0; offset < 2; ++offset)
                    {
                        auto& control = *members[4 * row + 2 + offset];
                        control.setTopLeftPosition(rightPairX + int(offset) * stride, control.getY());
                    }
            }
        }

        for (const auto& member : members)
            if (!member->layoutHidden) occupied = occupied.getUnion(member->getBounds());
    }
    controlBlockFrames.clear();
    const auto* page = findParentComponentOfClass<CrossoverRangePage>();
    const auto colour = independent || (page != nullptr && page->owner.config.moduleKey == "dyn")
        ? uiGreyDark : uiWhite;
    // Independent module blocks occupy their whole row both open and closed;
    // their painted edges must have the same 8px gap in either state.
    auto frame = independent ? getLocalBounds()
                             : (collapsed ? getLocalBounds().reduced(controlBlockFrameGap)
                                          : getControlBlockFrameBounds(occupied));
    if (!independent && colour != uiWhite) frame.setWidth(getWidth() - 2 * controlBlockFrameGap);
    if (collapsed || !occupied.isEmpty()) controlBlockFrames.push_back({frame, label, colour});
}

void CrossoverRangePage::groupBlockControls()
{
    if (owner.config.moduleKey != "chain") return;
    for (const auto& block : ava::modules::chain::blocks::descriptions)
    {
        const auto first = std::find_if(rows.begin(), rows.end(), [&] (const auto& row) { return row->blockIndex == int(block.id); });
        if (first == rows.end()) continue;
        const auto last = std::find_if(first, rows.end(), [&] (const auto& row) { return row->blockIndex != int(block.id); });
        const auto insertIndex = size_t(first - rows.begin());
        std::vector<std::unique_ptr<RowBase>> members;
        for (auto iter = first; iter != last; ++iter) members.push_back(std::move(*iter));
        auto group = std::make_unique<ParameterGroup>( "block." + juce::String(int(block.id)), std::move(members));
        group->blockIndex = int(block.id);
        group->label = block.name;
        group->collapseBlockInitially(group->label);
        group->columns = block.id == ava::modules::chain::blocks::Id::listen ? 4 : 0;
        if (owner.config.uiState != nullptr)
            group->bindCollapseState(*owner.config.uiState,
                (owner.config.collapseScope.isNotEmpty() ? owner.config.collapseScope : owner.config.moduleKey)
                + ".band-" + juce::String(int(rangeIndex + 1)) + ".groups");
        rows.erase(first, last);
        addAndMakeVisible(*group);
        rows.insert(rows.begin() + std::ptrdiff_t(insertIndex), std::move(group));
    }
}

CrossoverRangePage::CrossoverRangePage(CrossoverModuleComponent& ownerIn,
                                       const size_t rangeIndexIn)
    : owner(ownerIn),
      rangeIndex(rangeIndexIn)
{
    setName("page-" + juce::String(static_cast<int>(rangeIndex) + 1));
    if (owner.config.moduleKey == "dyn") collapseBlockInitially("DYN");
    if (owner.config.uiState != nullptr)
        bindCollapseState(*owner.config.uiState,
            (owner.config.collapseScope.isNotEmpty() ? owner.config.collapseScope : owner.config.moduleKey)
            + ".band-" + juce::String(int(rangeIndex + 1)) + ".page");

    addControlSpecs(owner.config.rangeControls, *this);
    if (owner.config.moduleKey == "chain") {
        size_t index=0;
        for(const auto& block:ava::modules::chain::blocks::descriptions)
            for(size_t n=0;n<block.rows && index<rows.size();++n)
            {
                rows[index]->blockIndex=int(block.id);
                if(block.id==ava::modules::chain::blocks::Id::gain) rows[index]->gainIndex=int(n);
                ++index;
            }
        jassert(index==rows.size());
        groupBlockControls();
        if(owner.config.getGainOrder) applyGainOrder(owner.config.getGainOrder(rangeIndex));
    }
    if(owner.config.moduleKey=="chain") {
        for(const auto& block:ava::modules::chain::blocks::descriptions) {
            const auto id=owner.config.makeRangeParameterId(rangeIndex,block.hiddenSuffix);
            listenToParameter(id);
        }
        refreshBlockVisibility();
    }
    refreshRows();
}

CrossoverRangePage::~CrossoverRangePage()
{
    cancelPendingUpdate();
    for (const auto& parameterId : listenedParameterIds)
        owner.valueTreeState.removeParameterListener(parameterId, this);
}

void CrossoverRangePage::refreshExternalState()
{
    if(owner.config.getGainOrder)
        applyGainOrder(owner.config.getGainOrder(rangeIndex));
    const auto previousPreferredHeight = getPreferredHeight();
    const auto visibilityChanged=refreshBlockVisibility();
    refreshRows();

    if (visibilityChanged || previousPreferredHeight != getPreferredHeight()) {
        owner.resized();
        resized();
    }
}

int CrossoverRangePage::getPreferredHeight() const
{
    const bool isDyn = owner.config.moduleKey == "dyn";
    if (isDyn)
    {
        if (isBlockCollapsed("DYN"))
            return controlBlockHeaderHeight;
        std::vector<ControlGridCell> cells;
        cells.reserve(rows.size());
        for (const auto& row : rows)
            if (!row->layoutHidden)
                cells.push_back({row.get(), 1,
                    bool(row->getProperties().getWithDefault("startNewControlRow", false))});
        const auto inset = controlBlockPadding + controlBlockFrameThickness;
        const auto innerWidth = juce::jmax(1, getWidth() - getUiGap(*this) - 2 * inset);
        const auto content = measureControlGrid(*this,
            {inset, controlBlockHeaderHeight + uiGap, innerWidth, 0}, cells);
        return content.getBottom() + uiGap + controlBlockFrameThickness;
    }
    return measureRows(rows, 0, juce::jmax(1, getWidth() - int(getProperties().getWithDefault("contentRightInset", 0)))).y
        + 2 * (getEditorContentInset(*this) - 2 * getUiGap(*this));
}

void CrossoverRangePage::resized()
{
    if (bool(getProperties().getWithDefault("flatBlocks", false))) return;
    const bool isDyn = owner.config.moduleKey == "dyn";
    if (isDyn)
    {
        controlBlockFrames.clear();
        const auto frameWidth = juce::jmax(0, getWidth() - getUiGap(*this));
        if (isBlockCollapsed("DYN"))
        {
            for (auto& row : rows) { row->setVisible(false); row->setBounds({}); }
            controlBlockFrames.push_back({{0, 0, frameWidth,
                controlBlockHeaderHeight}, "DYN", uiGreyDark});
        }
        else
        {
            std::vector<ControlGridCell> cells;
            cells.reserve(rows.size());
            for (auto& row : rows)
            {
                row->setVisible(!row->layoutHidden);
                if (!row->layoutHidden)
                    cells.push_back({row.get(), 1,
                        bool(row->getProperties().getWithDefault("startNewControlRow", false))});
                else row->setBounds({});
            }
            const auto inset = controlBlockPadding + controlBlockFrameThickness;
            const auto innerWidth = juce::jmax(1, frameWidth - 2 * inset);
            const auto occupied = layoutControlGrid(*this,
                {inset, controlBlockHeaderHeight + uiGap, innerWidth, 0}, cells);
            controlBlockFrames.push_back({{0, 0, frameWidth,
                occupied.getBottom() + uiGap + controlBlockFrameThickness}, "DYN", uiGreyDark});
        }
        controlBlockFrames.back().displayTitle = owner.getProperties()
            .getWithDefault("moduleDisplayTitle", "DYN").toString();
        return;
    }
    for (auto& row : rows) row->setVisible(!row->layoutHidden);
    auto bounds = getLocalBounds().withHeight(juce::jmax(getHeight(), getPreferredHeight()));
    bounds.removeFromTop(getEditorContentInset(*this) - 2 * getUiGap(*this));
    bounds.removeFromBottom(getEditorContentInset(*this));
    bounds.removeFromRight(int(getProperties().getWithDefault("contentRightInset", 0)));
    layoutRows(rows, 0, rows.size(), bounds);
    controlBlockFrames.clear();
}

void CrossoverRangePage::mouseDown(const juce::MouseEvent&)
{
    owner.clearFocus();
}

void CrossoverRangePage::handleAsyncUpdate()
{
    owner.synchroniseManualSoloMaskFromParameters();
    refreshExternalState();
}

void CrossoverRangePage::refreshRows()
{
    for (auto& row : rows)
        row->refreshExternalState();
}

void CrossoverRangePage::parameterChanged(const juce::String& parameterID, float)
{
    if (std::find(listenedParameterIds.begin(), listenedParameterIds.end(), parameterID) == listenedParameterIds.end())
        return;

    triggerAsyncUpdate();
}

std::unique_ptr<CrossoverModulePage> makeCrossoverRangePage(CrossoverModuleComponent& owner,
                                                            const size_t rangeIndex)
{
    return std::make_unique<CrossoverRangePage>(owner, rangeIndex);
}

juce::String CrossoverRangePage::getCrossoverRangeParameterId(const CrossoverControlSpec& spec) const
{
    const auto sourceRange = spec.sourceRangeIndex >= 0
        ? static_cast<size_t>(juce::jlimit(0, static_cast<int>(CrossoverModuleComponent::numRanges - 1), spec.sourceRangeIndex))
        : rangeIndex;
    return owner.config.makeRangeParameterId(sourceRange, spec.suffix);
}

void CrossoverRangePage::listenToParameter(const juce::String& id)
{
    if (std::find(listenedParameterIds.begin(), listenedParameterIds.end(), id) != listenedParameterIds.end()) return;
    listenedParameterIds.push_back(id);
    owner.valueTreeState.addParameterListener(id, this);
}

void CrossoverRangePage::addControlSpecs(const std::vector<CrossoverControlSpec>& specs, juce::Component& parent)
{
    for (const auto& spec : specs)
    {
        if(spec.kind==ControlKind::custom && owner.config.makeCustomControl) {
            auto row=std::make_unique<CustomRow>(owner.config.makeCustomControl(rangeIndex,spec.suffix));
            row->label=spec.label;row->setName(spec.label);row->hasTopGap=spec.hasTopGap;
            parent.addAndMakeVisible(*row);rows.push_back(std::move(row));continue;
        }
        const auto policy = owner.config.getControlPolicy(rangeIndex, spec.suffix);
        for (const auto* dependency : policy.dependencies)
            listenToParameter(owner.config.makeRangeParameterId(rangeIndex, dependency));

        if (spec.kind == ControlKind::toggle)
        {
            auto row = std::make_unique<ToggleRow>(*this, owner, getCrossoverRangeParameterId(spec), spec);
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            if (spec.startNewControlRow) row->getProperties().set("startNewControlRow", true);
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::choice)
        {
            const auto auxiliaryId = juce::String(policy.metadata.auxiliarySuffix).isNotEmpty()
                ? owner.config.makeRangeParameterId(rangeIndex, policy.metadata.auxiliarySuffix) : juce::String {};
            auto row = std::make_unique<ChoiceRow>(owner, rangeIndex, getCrossoverRangeParameterId(spec), auxiliaryId, spec);
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            if (spec.startNewControlRow) row->getProperties().set("startNewControlRow", true);
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::time)
        {
            const auto valueId = getCrossoverRangeParameterId(spec);
            const auto modeId = owner.config.makeRangeParameterId(rangeIndex, policy.metadata.modeSuffix);
            const auto syncId = owner.config.makeRangeParameterId(rangeIndex, policy.metadata.noteSuffix);
            auto row = std::make_unique<TimeRow>(owner, rangeIndex, valueId, modeId, syncId, spec);
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            if (spec.startNewControlRow) row->getProperties().set("startNewControlRow", true);
            for (const auto& id : {valueId, modeId, syncId}) listenToParameter(id);
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::readout)
        {
            const auto degreeId = getCrossoverRangeParameterId(spec);
            const auto flipId = owner.config.makeRangeParameterId(rangeIndex, spec.modeSuffix);
            auto row = std::make_unique<ReadoutRow>(owner, degreeId, flipId, spec);
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            if (spec.startNewControlRow) row->getProperties().set("startNewControlRow", true);
            for (const auto& id : {degreeId, flipId, flipId + ".bypass"})
                if (owner.valueTreeState.getParameter(id) != nullptr) listenToParameter(id);
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        const auto auxiliaryToggleId = policy.metadata.auxiliarySuffix != nullptr
                                           && juce::String(policy.metadata.auxiliarySuffix).isNotEmpty()
            ? owner.config.makeRangeParameterId(rangeIndex, policy.metadata.auxiliarySuffix)
            : juce::String {};
        auto row = std::make_unique<ParameterRow>(*this,
                                                  owner,
                                                  getCrossoverRangeParameterId(spec),
                                                  auxiliaryToggleId,
                                                  spec);
        row->label = spec.label;
        row->setName(spec.label);
        row->controlsInRow = spec.controlsInRow;
        if (spec.startNewControlRow) row->getProperties().set("startNewControlRow", true);
        parent.addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

}

void CrossoverRangePage::applyGainOrder(const ava::modules::chain::gain::Order& order)
{
    if(!ava::modules::chain::gain::valid(order) || displayedGainOrder==order) return;
    auto* gainRows = &rows;
    ParameterGroup* gainGroup = nullptr;
    for (const auto& row : rows)
        if (auto* group = dynamic_cast<ParameterGroup*>(row.get()); group != nullptr
            && group->blockIndex == int(ava::modules::chain::blocks::Id::gain))
        { gainGroup = group; gainRows = &group->members; break; }
    const auto first=std::find_if(gainRows->begin(),gainRows->end(),[](const auto& row){return row->gainIndex>=0;});
    if(first==gainRows->end() || size_t(gainRows->end()-first)<order.size()) return;
    const auto last=first+std::ptrdiff_t(order.size());
    const auto topGap=(*first)->hasTopGap;
    std::array<int,ava::modules::chain::gain::count> positions {};
    for(size_t i=0;i<order.size();++i) positions[size_t(order[i])]=int(i);
    std::stable_sort(first,last,[&](const auto& a,const auto& b){return positions[size_t(a->gainIndex)]<positions[size_t(b->gainIndex)];});
    for(auto row=first;row!=last;++row) (*row)->hasTopGap = row != first || topGap;
    displayedGainOrder=order;
    if (gainGroup != nullptr) gainGroup->resized();
    resized();repaint();
}
bool CrossoverRangePage::refreshBlockVisibility()
{
    if(owner.config.moduleKey!="chain") return false;
    bool changed=false;
    int previousVisibleBlock=-1;
    for(size_t i=0;i<rows.size();++i) {
        auto& row=*rows[i];
        if(row.blockIndex<0) continue;
        const auto& block=ava::modules::chain::blocks::descriptions[size_t(row.blockIndex)];
        // In the independent MODULES stack the header stays visible while
        // audio processing is bypassed, so BP can be clicked again.
        const auto hidden = !bool(getProperties().getWithDefault("flatBlocks", false))
            && readRawParameter(owner.valueTreeState,
                owner.config.makeRangeParameterId(rangeIndex,block.hiddenSuffix),0)>=.5f;
        if(row.layoutHidden!=hidden) {
            row.layoutHidden=hidden;
            row.setVisible(!hidden);
            if(hidden) row.setBounds({});
            changed=true;
        }
        if(!hidden && row.blockIndex!=previousVisibleBlock) {
            const auto gap = previousVisibleBlock >= 0;
            changed=changed || row.hasTopGap!=gap;
            row.hasTopGap=gap;
            previousVisibleBlock=row.blockIndex;
        }
    }
    return changed;
}

void CrossoverRangePage::hideIndependentBlocks()
{
    if (!bool(getProperties().getWithDefault("flatBlocks", false))) return;
    for (auto& row : rows) row->setVisible(false);
}
std::vector<std::pair<int, juce::Component*>> CrossoverRangePage::independentBlocks(juce::Component& parent, int width)
{
    getProperties().set("flatBlocks", true);
    refreshBlockVisibility();
    std::vector<std::pair<int, juce::Component*>> result;
    for (auto& row : rows)
    {
        if (row->layoutHidden) { row->setVisible(false); continue; }
        row->getProperties().set("independentBlock", true);
        // CHAIN rows are reparented out of CrossoverModuleComponent when
        // shown in MODULES; keep their local-thumb painting policy with them.
        row->getProperties().set("paintRangeLocally", true);
        if (row->getParentComponent() != &parent) parent.addAndMakeVisible(*row);
        row->setVisible(true);
        row->setSize(width, row->getLayoutHeight(width));
        result.emplace_back(row->blockIndex, row.get());
    }
    return result;
}
