#include "RangePage.h"
#include "UiSupport.h"
#include "DynControlGroups.h"
#include "../shell/EditorSetup.h"
#include "../shell/LocalParameterControl.h"

#include <algorithm>
#include <cstddef>

using namespace crossover_ui;
using ControlKind = CrossoverModuleComponent::ControlKind;

namespace
{
constexpr int groupInset = dynControlGroupOutset; // One pixel frame outside the group clearance.
constexpr int groupPetalHeight = 8;

class GroupPetal final : public juce::Button
{
public:
    GroupPetal() : juce::Button("Toggle parameter group")
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }
    void setExpanded(bool value) { expanded = value; repaint(); }
    bool hitTest(int x, int y) override { return outline().contains(float(x), float(y)); }
    void paintButton(juce::Graphics& g, const bool hover, const bool down) override
    {
        const auto amount=hoverAnimation.amount(isEnabled() && (hover || down));
        ControlAppearance style(expanded,amount);
        style.paint(g,outline());
    }
private:
    juce::Path outline(const float inset = 0.0f) const
    {
        const auto w = float(getWidth()) - inset, h = float(getHeight()) - inset;
        const auto bevel = juce::jmin(4.0f, w * 0.5f);
        juce::Path path;
        if (expanded)
        {
            path.startNewSubPath(inset, h);
            path.lineTo(inset + bevel, inset);
            path.lineTo(w - bevel, inset);
            path.lineTo(w, h);
        }
        else
        {
            path.startNewSubPath(inset, inset);
            path.lineTo(inset + bevel, h);
            path.lineTo(w - bevel, h);
            path.lineTo(w, inset);
        }
        path.closeSubPath();
        return path;
    }
    HoverAnimation hoverAnimation { *this };
    bool expanded = false;
};

template <typename Rows>
int getRowsHeight(const Rows& rows, const size_t firstRow, const size_t lastRow)
{
    auto height = 0;

    for (size_t index = firstRow; index < lastRow;)
    {
        if(rows[index]->layoutHidden) { ++index; continue; }
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
        if(rows[index]->layoutHidden) { ++index; continue; }
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
            auto itemBounds = controlBounds.removeFromLeft(isLast ? controlBounds.getWidth() : controlWidth);
            rows[index + offset]->setBounds(itemBounds.expanded(rows[index + offset]->getHorizontalOutset(), 0));

            if (! isLast)
                controlBounds.removeFromLeft(juce::jmin(parameterGap, controlBounds.getWidth()));
        }

        index += controlsInRow;
    }
}
}

CrossoverRangePage::DisclosureGroup::DisclosureGroup(CrossoverRangePage& pageIn, juce::String keyIn,
    std::vector<size_t> definingIndicesIn, std::vector<std::unique_ptr<RowBase>> membersIn)
    : page(pageIn),
      stateKey("group." + juce::String(int(page.rangeIndex)) + "." + keyIn),
      definingIndices(std::move(definingIndicesIn)), members(std::move(membersIn)),
      petal(std::make_unique<GroupPetal>())
{
    setName("group-" + keyIn);
    topGapMultiplier = members.front()->topGapMultiplier;
    members.front()->topGapMultiplier = 0;
    slidingClip.setInterceptsMouseClicks(false, true);
    slidingBody.setInterceptsMouseClicks(false, true);
    addChildComponent(slidingClip);
    slidingClip.addAndMakeVisible(slidingBody);
    for (size_t index = 0; index < members.size(); ++index)
    {
        const auto defining = std::find(definingIndices.begin(), definingIndices.end(), index) != definingIndices.end();
        members[index]->paintsRowBackground = defining;
        if (defining) addChildComponent(*members[index]);
        else slidingBody.addChildComponent(*members[index]);
    }
    petal->onClick = [this] { toggle(); };
    petal->setTooltip("Expand / collapse parameter group");
    addAndMakeVisible(*petal);
    expanded = getBool(page.owner.valueTreeState.state, "dyn", stateKey, false);
    animation.snap(expanded);
    animation.onUpdate = [this] { updateVisibility(); page.triggerAsyncUpdate(); };
    updateVisibility();
}

int CrossoverRangePage::DisclosureGroup::getPreferredHeight() const
{
    const auto openHeight = groupInset + getRowsHeight(members, 0, members.size()) + dynControlGroupGap + groupPetalHeight;
    auto closedHeight = groupInset + dynControlGroupGap + groupPetalHeight + int(definingIndices.size() - 1) * verticalGap;
    for (const auto index : definingIndices) closedHeight += members[index]->getPreferredHeight();
    return juce::roundToInt(closedHeight + (openHeight - closedHeight) * animation.value());
}

int CrossoverRangePage::DisclosureGroup::getTopGap() const noexcept
{
    return juce::jmax(0, RowBase::getTopGap() - groupInset);
}

int CrossoverRangePage::DisclosureGroup::getHorizontalOutset() const noexcept
{
    return groupInset;
}

void CrossoverRangePage::DisclosureGroup::updateVisibility()
{
    for (size_t index = 0; index < members.size(); ++index)
    {
        const auto defining = std::find(definingIndices.begin(), definingIndices.end(), index) != definingIndices.end();
        members[index]->setVisible(defining || expanded || animation.value() > 0.0);
        members[index]->setAlpha(1.0f);
        members[index]->setInterceptsMouseClicks(defining || ! animation.isRunning(), defining || ! animation.isRunning());
    }
    slidingClip.setVisible(animation.value() > 0.0);
    static_cast<GroupPetal&>(*petal).setExpanded(expanded);
}

void CrossoverRangePage::DisclosureGroup::refreshExternalState()
{
    for (auto& row : members) row->refreshExternalState();
    const auto restored = getBool(page.owner.valueTreeState.state, "dyn", stateKey, false);
    if (restored != expanded)
    {
        setExpanded(restored, false);
        resized();
        repaint();
    }
}

void CrossoverRangePage::DisclosureGroup::toggle()
{
    page.owner.clearFocus();
    setExpanded(! expanded, true);
    setBool(page.owner.valueTreeState.state, "dyn", stateKey, expanded);
    if (page.owner.config.markParametersDirty) page.owner.config.markParametersDirty();
    updateVisibility();
    // The total height can stay unchanged when the viewport is taller than the
    // content, so lay out this group explicitly as well as refreshing the viewport.
    page.owner.resized();
    page.resized();
    resized();
    repaint();
}

void CrossoverRangePage::DisclosureGroup::setExpanded(const bool expandedIn, const bool animate)
{
    expanded = expandedIn;
    if (animate) animation.setExpanded(expanded);
    else animation.snap(expanded);
    updateVisibility();
}

void CrossoverRangePage::DisclosureGroup::resized()
{
    auto bounds = getLocalBounds();
    auto petalBounds = bounds.removeFromBottom(groupPetalHeight);
    petal->setBounds(petalBounds.reduced(juce::roundToInt(groupInset * (1.0 - animation.value())), 0));
    bounds.removeFromTop(groupInset);
    bounds.removeFromBottom(dynControlGroupGap);
    const auto content = bounds.reduced(groupInset, 0);
    const auto fullHeight = getRowsHeight(members, 0, members.size());
    auto compactHeight = int(definingIndices.size() - 1) * verticalGap;
    for (const auto index : definingIndices) compactHeight += members[index]->getPreferredHeight();
    const auto offset = juce::roundToInt((fullHeight - compactHeight) * (1.0 - animation.value()));
    slidingClip.setBounds(content);
    slidingBody.setBounds(0, -offset, content.getWidth(), fullHeight);
    layoutRows(members, 0, members.size(), { 0, 0, content.getWidth(), fullHeight }, parameterGap);
    auto compactY = 0;
    for (const auto index : definingIndices)
    {
        auto& row = *members[index];
        row.setTopLeftPosition(content.getX(), content.getY()
            + juce::roundToInt(compactY + (row.getY() - compactY) * animation.value()));
        compactY += row.getPreferredHeight() + verticalGap;
        row.toFront(false);
    }
    petal->toFront(false);
}

void CrossoverRangePage::DisclosureGroup::paint(juce::Graphics& g)
{
    if (animation.value() <= 0.0) return;
    g.setColour(uiWhite);
    const auto height = getHeight();
    const auto width = getWidth();
    g.fillRect(0, 0, width, 1);
    g.fillRect(0, 0, 1, height);
    g.fillRect(juce::jmax(0, width - 1), 0, 1, height);
}

void CrossoverRangePage::groupDynControls()
{
    if (owner.config.moduleKey != "dyn") return;
    for (const auto& spec : dynControlGroups)
    {
        const auto first = std::find_if(rows.begin(), rows.end(), [&] (const auto& row) { return row->label == spec.firstLabel; });
        if (first == rows.end()) continue;
        auto last = std::find_if(first, rows.end(), [&] (const auto& row) { return row->label == spec.lastLabel; });
        if (last == rows.end()) continue;
        if (juce::String(spec.optionalLastLabel).isNotEmpty())
        {
            const auto extended = std::find_if(std::next(last), rows.end(), [&] (const auto& row) { return row->label == spec.optionalLastLabel; });
            if (extended != rows.end()) last = extended;
        }
        const auto defining = std::find_if(first, std::next(last), [&] (const auto& row) { return row->label == spec.definingLabel; });
        if (defining == std::next(last)) continue;
        const auto insertIndex = size_t(first - rows.begin());
        std::vector<size_t> definingIndices { size_t(defining - first) };
        if (juce::String(spec.secondDefiningLabel).isNotEmpty())
        {
            const auto second = std::find_if(first, std::next(last), [&] (const auto& row) { return row->label == spec.secondDefiningLabel; });
            if (second == std::next(last)) continue;
            definingIndices.push_back(size_t(second - first));
        }
        std::vector<std::unique_ptr<RowBase>> members;
        for (auto iter = first; iter != std::next(last); ++iter) members.push_back(std::move(*iter));
        auto group = std::make_unique<DisclosureGroup>(*this, spec.key, std::move(definingIndices), std::move(members));
        rows.erase(first, std::next(last));
        addAndMakeVisible(*group);
        rows.insert(rows.begin() + std::ptrdiff_t(insertIndex), std::move(group));
    }
}

CrossoverRangePage::CrossoverRangePage(CrossoverModuleComponent& ownerIn,
                                       const size_t rangeIndexIn,
                                       juce::Colour accent)
    : owner(ownerIn),
      rangeIndex(rangeIndexIn),
      moduleHeading(uiGreyLight)
{
    juce::ignoreUnused(accent);
    setName("page-" + juce::String(static_cast<int>(rangeIndex) + 1));

    if (owner.config.showModuleHeading)
    {
        moduleHeading.setButtonText(owner.config.moduleKey.toUpperCase());
        moduleHeading.setTextJustification(juce::Justification::centred);
        moduleHeading.setActiveFill(false);
        moduleHeading.setToggleActiveFillEnabled(false);
        moduleHeading.setLongPressPromptActions([this]
        {
            if (owner.config.onModuleCloseRequest != nullptr)
                owner.config.onModuleCloseRequest();
        });
        moduleHeading.setLongPressPrimaryPromptIcon("x");
        addAndMakeVisible(moduleHeading);
    }

    pinnedTail = std::make_unique<juce::Component>();
    addControlSpecs(owner.config.rangeControls, *this);
    groupDynControls();
    tailRowStart = rows.size();
    if (owner.config.getBlockOrder) {
        size_t index=0;
        for(const auto& block:tls::blocks::descriptions)
            for(size_t n=0;n<block.rows && index<rows.size();++n)
            {
                rows[index]->blockIndex=int(block.id);
                if(block.id==tls::blocks::Id::gain) rows[index]->gainIndex=int(n);
                ++index;
            }
        jassert(index==rows.size());
        applyBlockOrder(owner.config.getBlockOrder(rangeIndex));
        if(owner.config.getGainOrder) applyGainOrder(owner.config.getGainOrder(rangeIndex));
    }
    addControlSpecs(owner.config.rangeTailControls, *pinnedTail);
    if(owner.config.moduleKey=="tls") {
        for(const auto& block:tls::blocks::descriptions) {
            const auto id=owner.config.makeRangeParameterId(rangeIndex,block.hiddenSuffix);
            listenedParameterIds.push_back(id);
            owner.valueTreeState.addParameterListener(id,this);
        }
        refreshBlockVisibility();
    }
    updateTimeModeControls();

}

CrossoverRangePage::~CrossoverRangePage()
{
    for (const auto& parameterId : listenedParameterIds)
        owner.valueTreeState.removeParameterListener(parameterId, this);
}

void CrossoverRangePage::refreshExternalState()
{
    if (owner.config.getBlockOrder && !previewingBlockOrder)
        applyBlockOrder(owner.config.getBlockOrder(rangeIndex));
    if(owner.config.getGainOrder && !previewingGainOrder)
        applyGainOrder(owner.config.getGainOrder(rangeIndex));
    const auto previousPreferredHeight = getPreferredHeight();
    const auto visibilityChanged=refreshBlockVisibility();
    updateToggleLabels();
    updateTimeModeControls();

    if (visibilityChanged || previousPreferredHeight != getPreferredHeight()) {
        owner.resized();
        resized();
    }
}

int CrossoverRangePage::getPreferredHeight() const
{
    const auto flatHeight = getFlatPreferredHeight();
    return flatHeight;
}

int CrossoverRangePage::getFlatPreferredHeight() const
{
    auto height = 0;
    const auto moduleHeadingIsPinned = owner.config.showModuleHeading && owner.config.pinModuleHeading;

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
    auto bounds = getLocalBounds().withHeight(juce::jmax(getHeight(), getFlatPreferredHeight()));
    if (owner.config.moduleKey == "dyn") bounds.reduce(groupInset, 0);
    if (owner.config.showModuleHeading && ! owner.config.pinModuleHeading)
    {
        moduleHeading.setBounds(bounds.removeFromTop(rowHeight));
    }

    layoutRows(rows,
                                             0,
                                             tailRowStart,
                                             bounds,
                                             parameterGap);
}

void CrossoverRangePage::mouseDown(const juce::MouseEvent& event)
{
    owner.clearFocus();
    if(event.mods.isPopupMenu() && owner.config.showArrangePrompt) {
        showBlockArrange();
        return;
    }
    if (! event.mods.isPopupMenu() || owner.config.moduleKey != "dyn"
        || owner.config.showChoicePrompt == nullptr)
        return;
    auto anchor = juce::Rectangle<int>(160, rowHeight);
    anchor.setCentre(event.getEventRelativeTo(this).getPosition());
    bool anyGroup=false, allExpanded=true, allCollapsed=true;
    for(const auto& row:rows)
        if(const auto* group=dynamic_cast<const DisclosureGroup*>(row.get())) {
            anyGroup=true;
            allExpanded &= group->expanded;
            allCollapsed &= !group->expanded;
        }
    const auto selected=anyGroup ? (allCollapsed?0:(allExpanded?1:-1)) : -1;
    owner.config.showChoicePrompt(owner.getLocalArea(this, anchor),
        { "COLLAPSE-ALL", "EXPAND-ALL" }, selected, { true, true }, juce::Justification::centred,
        [safeThis = juce::Component::SafePointer<CrossoverRangePage>(this)] (const int choice)
        {
            if (safeThis != nullptr && (choice == 0 || choice == 1))
                safeThis->setAllGroupsExpanded(choice == 1);
        }, {}, {});
}

void CrossoverRangePage::handleAsyncUpdate()
{
    owner.resized();
    resized();
    repaint();
}

void CrossoverRangePage::setAllGroupsExpanded(const bool expanded)
{
    owner.clearFocus();
    for (auto& row : rows)
        if (auto* group = dynamic_cast<DisclosureGroup*>(row.get()))
        {
            group->setExpanded(expanded, true);
            setBool(owner.valueTreeState.state, "dyn", group->stateKey, expanded);
        }
    if (owner.config.markParametersDirty) owner.config.markParametersDirty();
    owner.resized();
    resized();
    for (auto& row : rows) { row->resized(); row->repaint(); }
}

void CrossoverRangePage::expandContentForSvgExport()
{
    svgExportGroupStates.clear();

    for (auto& row : rows)
        if (auto* group = dynamic_cast<DisclosureGroup*>(row.get()))
            svgExportGroupStates.push_back(group->expanded);

    // Settle in-flight disclosure animations before exporting, or rows can be clipped.
    for (auto& row : rows)
        if (auto* group = dynamic_cast<DisclosureGroup*>(row.get()))
            group->setExpanded(true, false);

    refreshSvgExportLayout();

    for (auto& row : rows)
    {
        row->resized();
        row->repaint();
    }
}

void CrossoverRangePage::restoreContentAfterSvgExport()
{
    auto state = svgExportGroupStates.begin();

    for (auto& row : rows)
        if (auto* group = dynamic_cast<DisclosureGroup*>(row.get()))
        {
            const auto expanded = state != svgExportGroupStates.end() && *state;

            if (state != svgExportGroupStates.end())
                ++state;

            group->setExpanded(expanded, false);
        }

    svgExportGroupStates.clear();
    refreshSvgExportLayout();

    for (auto& row : rows)
    {
        row->resized();
        row->repaint();
    }
}

void CrossoverRangePage::refreshSvgExportLayout()
{
    // The module sizes this page from its preferred height, so the owner has to
    // lay out again before the export sees the temporarily changed groups.
    owner.resized();
    resized();
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
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            row->exclusiveScope = spec.exclusiveScope;
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::toggle)
        {
            auto row = std::make_unique<ToggleRow>(*this, owner, getCrossoverRangeParameterId(spec), spec);
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            row->exclusiveScope = spec.exclusiveScope;
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::choice)
        {
            const auto auxiliaryId = juce::String(spec.auxiliaryToggleSuffix).isNotEmpty()
                ? owner.config.makeRangeParameterId(rangeIndex, spec.auxiliaryToggleSuffix) : juce::String {};
            auto row = std::make_unique<ChoiceRow>(owner, getCrossoverRangeParameterId(spec), auxiliaryId, spec);
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            row->exclusiveScope = spec.exclusiveScope;
            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
            continue;
        }

        if (spec.kind == ControlKind::inactive)
        {
            auto row = std::make_unique<InactiveRow>(spec);
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            row->exclusiveScope = spec.exclusiveScope;
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
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            row->exclusiveScope = spec.exclusiveScope;
            listenedParameterIds.push_back(valueId);
            listenedParameterIds.push_back(modeId);
            listenedParameterIds.push_back(syncId);
            owner.valueTreeState.addParameterListener(valueId, this);
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
            row->label = spec.label;
            row->setName(spec.label);
            row->controlsInRow = spec.controlsInRow;
            row->exclusiveScope = spec.exclusiveScope;
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
                                                  spec);
        row->label = spec.label;
        row->setName(spec.label);
        row->controlsInRow = spec.controlsInRow;
        row->exclusiveScope = spec.exclusiveScope;
        for (const auto& dependencyId : row->enabledWhenAnyNonZeroIds)
        {
            listenedParameterIds.push_back(dependencyId);
            owner.valueTreeState.addParameterListener(dependencyId, this);
        }

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

        parent.addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

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

void CrossoverRangePage::applyBlockOrder(const tls::blocks::Order& order)
{
    if(!tls::blocks::valid(order) || tailRowStart==0 || displayedBlockOrder==order) return;
    std::array<int,tls::blocks::count> positions {};
    for(size_t index=0;index<order.size();++index) positions[size_t(order[index])]=int(index);
    std::stable_sort(rows.begin(),rows.begin()+std::ptrdiff_t(tailRowStart),[&](const auto& a,const auto& b) {
        return positions[size_t(a->blockIndex)]<positions[size_t(b->blockIndex)];
    });
    for(size_t i=0;i<tailRowStart;++i)
        if(i==0 || rows[i-1]->blockIndex!=rows[i]->blockIndex)
            rows[i]->topGapMultiplier=i==0?0:2;
    displayedBlockOrder=order;
    refreshBlockVisibility();
    resized();
    repaint();
}
void CrossoverRangePage::showBlockArrange()
{
    if(!owner.config.showArrangePrompt || !owner.config.getBlockOrder) return;
    const auto safe=juce::Component::SafePointer<CrossoverRangePage>(this);
    owner.config.showArrangePrompt(rangeIndex, owner.config.getBlockOrder(rangeIndex),
        [safe](const tls::blocks::Order& order) {
            if(safe==nullptr) return;
            safe->previewingBlockOrder=true;
            safe->applyBlockOrder(order);
        },
        [safe](const tls::blocks::Order& order) {
            if(safe==nullptr) return;
            if(safe->owner.config.setBlockOrder) safe->owner.config.setBlockOrder(safe->rangeIndex,order);
            safe->previewingBlockOrder=false;
            safe->applyBlockOrder(order);
        },
        [safe] {
            if(safe==nullptr) return;
            safe->previewingBlockOrder=false;
            safe->applyBlockOrder(safe->owner.config.getBlockOrder(safe->rangeIndex));
        });
}

void CrossoverRangePage::applyGainOrder(const tls::gain::Order& order)
{
    if(!tls::gain::valid(order) || displayedGainOrder==order) return;
    const auto first=std::find_if(rows.begin(),rows.end(),[](const auto& row){return row->gainIndex>=0;});
    if(first==rows.end() || size_t(rows.end()-first)<order.size()) return;
    const auto last=first+std::ptrdiff_t(order.size());
    const auto topGap=(*first)->topGapMultiplier;
    std::array<int,tls::gain::count> positions {};
    for(size_t i=0;i<order.size();++i) positions[size_t(order[i])]=int(i);
    std::stable_sort(first,last,[&](const auto& a,const auto& b){return positions[size_t(a->gainIndex)]<positions[size_t(b->gainIndex)];});
    for(auto row=first;row!=last;++row) (*row)->topGapMultiplier=row==first?topGap:1;
    displayedGainOrder=order;
    resized();repaint();
}
void CrossoverRangePage::showGainArrange()
{
    if(!owner.config.showGainArrangePrompt || !owner.config.getGainOrder) return;
    const auto safe=juce::Component::SafePointer<CrossoverRangePage>(this);
    owner.config.showGainArrangePrompt(rangeIndex, owner.config.getGainOrder(rangeIndex),
        [safe](const tls::gain::Order& order) {
            if(safe==nullptr) return;
            safe->previewingGainOrder=true;
            safe->applyGainOrder(order);
        },
        [safe](const tls::gain::Order& order) {
            if(safe==nullptr) return;
            if(safe->owner.config.setGainOrder) safe->owner.config.setGainOrder(safe->rangeIndex,order);
            safe->previewingGainOrder=false;
            safe->applyGainOrder(order);
        },
        [safe] {
            if(safe==nullptr) return;
            safe->previewingGainOrder=false;
            safe->applyGainOrder(safe->owner.config.getGainOrder(safe->rangeIndex));
        });
}

bool CrossoverRangePage::refreshBlockVisibility()
{
    if(owner.config.moduleKey!="tls") return false;
    bool changed=false;
    int previousVisibleBlock=-1;
    for(size_t i=0;i<tailRowStart;++i) {
        auto& row=*rows[i];
        if(row.blockIndex<0) continue;
        const auto& block=tls::blocks::descriptions[size_t(row.blockIndex)];
        const auto hidden=readRawParameter(owner.valueTreeState,
            owner.config.makeRangeParameterId(rangeIndex,block.hiddenSuffix),0)>=.5f;
        if(row.layoutHidden!=hidden) {
            row.layoutHidden=hidden;
            row.setVisible(!hidden);
            if(hidden) row.setBounds({});
            changed=true;
        }
        if(!hidden && row.blockIndex!=previousVisibleBlock) {
            const auto gap=previousVisibleBlock<0?0:2;
            changed=changed || row.topGapMultiplier!=gap;
            row.topGapMultiplier=gap;
            previousVisibleBlock=row.blockIndex;
        }
    }
    return changed;
}
