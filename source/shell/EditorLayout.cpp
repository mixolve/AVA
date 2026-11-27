#include "Editor.h"
#include "FilterSection.h"
#include "../crossover/Editor.h"
#include "Metrics.h"
#include "EditorSetup.h"
#include "PresetSection.h"
#include "../routing/Panel.h"
#include "OscPanel.h"
#include "EditorStateSchema.h"

void AvaAudioProcessorEditor::paint(juce::Graphics& g)
{
    paintPluginBackground(g);
}

void AvaAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    const auto paintOuterFrame = [this, &g]
    {
        const auto bounds = getLocalBounds();
        constexpr int thickness = 2;
        if (bounds.getWidth() < thickness * 2 || bounds.getHeight() < thickness * 2) return;
        g.setColour(juce::Colour(0xff222222));
        g.fillRect(bounds.withHeight(thickness));
        g.fillRect(bounds.withTop(bounds.getBottom() - thickness));
        g.fillRect(bounds.withWidth(thickness));
        g.fillRect(bounds.withLeft(bounds.getRight() - thickness));
    };
    if (activeInstanceEditor != nullptr) { paintOuterFrame(); return; }
    const auto modulePageVisible = !hasUnitPage();
    {
    const juce::Graphics::ScopedSaveState saved(g);
    // Arrangement rows are intentionally not opaque. Do not paint the module's
    // range indicators over their surface during the editor's overlay pass.
    // The scrolling module viewport remains outside this clipped region.
    if (textPromptOverlay != nullptr
        && bool(textPromptOverlay->getProperties().getWithDefault("hidesModuleContent", false))
        && !bool(textPromptOverlay->getProperties().getWithDefault("paintControlIndicators", false)))
        g.excludeClipRegion(getModuleViewportBounds().expanded(0, 2 * getUiGap(*this)));
    ValueBoxComponent::paintRangeIndicators(g, *this, [this, modulePageVisible] (const juce::Component& component)
    {
        return modulePageVisible || (&component != crossoverEditor.get() && &component != blockChainModuleEditor.get()
            && &component != dynModuleEditor.get() && &component != &filterViewport);
    });
    if (searchFlashOn && searchFlashBlockTitle.isEmpty())
        if (auto* target = searchFlashTarget.getComponent(); target != nullptr && target->isShowing())
        {
            auto region = getLocalArea(target, target->getLocalBounds());
            const auto clip = unitPage == UnitPage::slots ? slotsViewport.getBounds() : filterViewport.getBounds();
            if (!clip.isEmpty() && clip.intersects(region))
            {
                const juce::Graphics::ScopedSaveState searchFlashClip(g);
                g.reduceClipRegion(clip);
                g.setColour(uiWhite.withAlpha(0.2f));
                g.fillRect(region);
                g.setColour(uiWhite);
                g.drawRect(region, 2);
            }
        }
    }
    // This is the final editor overlay so child panels cannot cover the
    // two-pixel #222222 plugin frame. It does not change window dimensions.
    paintOuterFrame();
}

int AvaAudioProcessorEditor::getActiveFilterContentHeight() const
{
    return moduleContent.getHeight();
}

void AvaAudioProcessorEditor::layoutGlobalSections(juce::Rectangle<int>& bounds)
{
    // A full-size overlay only hit-tests the 16px top margin.
    // No sidebar column or extra left padding is reserved.
    juce::ignoreUnused(bounds);
    if (windowDragHandle != nullptr)
        windowDragHandle->setBounds(getLocalBounds());
}

void AvaAudioProcessorEditor::applyPromptVisibility()
{
    if (textPromptOverlay != nullptr
        && bool(textPromptOverlay->getProperties().getWithDefault("hidesModuleContent", false)))
    {
        for (auto* component : { blockChainModuleEditor.get(), dynModuleEditor.get() })
            if (component != nullptr) component->setVisible(false);
        filterViewport.setVisible(false);
    }
}

void AvaAudioProcessorEditor::finalizeLayout() noexcept
{
    applyPromptVisibility();
    applyDockVisibility();
    syncMainScrollBar();
    // Keep the fixed quick-access row in front of the scrolling page but
    // behind prompts and the active nested editor.
    quickAccessStrip.toFront(false);
    dockSortToggle.toFront(false);
    dockCollapseToggle.toFront(false);
    dockVerticalToggle.toFront(false);
    dockClipIndicator.toFront(false);
    dockSortOverlay.toFront(false);
    updatePinnedSearchPosition();
    // Clear the previous positions when controls move or pages change.
    repaint();
    if (crossoverEditor != nullptr && !hasUnitPage())
        crossoverEditor->toFront(false);
    if (pinnedRoutingViewport.isVisible()) pinnedRoutingViewport.toFront(false);
    if (windowDragHandle != nullptr) windowDragHandle->toFront(false);
    for (auto& prompt : suspendedPromptOverlays)
    {
        prompt->setBounds(getLocalBounds());
        prompt->toFront(false);
    }
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
    if (routingInstanceId == 0 && pinnedRoutingViewport.isVisible())
        pinnedRoutingViewport.toFront(false);

    storeEditorStateToValueTree();
}

void AvaAudioProcessorEditor::resized()
{
    if (presetsSection == nullptr
        || globalBypassButton == nullptr
        || oscPanel == nullptr
        || undoButton == nullptr
        || redoButton == nullptr
        || abControl == nullptr
        || abCopyButton == nullptr
        || sortFiltersButton == nullptr
        || aboutControl == nullptr
        || routingControl == nullptr
        || slotsControl == nullptr)
        return;

    if (addFilterButton == nullptr)
        return;

    for (const auto& section : filterSections)
    {
        if (section == nullptr)
            return;
    }

    if (updateEditorResizeLimits()) return;
    // setBounds/setSize can clamp the viewport before moduleContent is laid
    // out. Do not treat that temporary position as a user scroll change.
    const juce::ScopedValueSetter<bool> layoutGuard(layingOutEditor, true);
    auto bounds = getLocalBounds();

    const auto editorInsetTop = 2 * getUiGap(*this);
    const auto editorInsetBottom = getEditorContentInset(*this);

    bounds.removeFromLeft(getUiGap(*this));
    bounds.removeFromRight(getUiGap(*this) + editorScrollBarArea);

    bounds.removeFromBottom(editorInsetBottom);
    bounds.removeFromTop(editorInsetTop);

    layoutGlobalSections(bounds);
    layoutQuickAccessStrip();
    const auto canvas = getLocalBounds();
    bounds = bounds.withY(canvas.getY() + editorInsetTop)
        .withHeight(canvas.getHeight() - editorInsetTop - editorInsetBottom);
    if (hasUnitPage())
    {
        const auto popupBounds = unitPage == UnitPage::about ? bounds.withBottom(getHeight())
            : canvas.withTrimmedTop(editorInsetTop).withTrimmedBottom(getUiGap(*this)).reduced(getUiGap(*this), 0);
        layoutUnitPage(popupBounds);
        finalizeLayout();
        return;
    }
    bounds.setRight(canvas.getRight() - editorScrollBarArea);
    layoutCrossoverSection(bounds);

    layoutUnifiedBlocks(bounds);

    // Reposition the UNIT-owned PIN ROUTING tiles in this same layout pass.
    // Otherwise the first expanded frame paints their compact-dock positions
    // and the periodic timer visibly moves them to the right afterwards.
    if (routingInstanceId == 0 && !isDockOnly())
        refreshPinnedRoutingStrip();

    finalizeLayout();
}

void AvaAudioProcessorEditor::layoutUnitPage(juce::Rectangle<int> bounds)
{
    if (unitPage == UnitPage::about)
    {
        if (aboutPanel != nullptr) aboutPanel->setBounds(bounds);
        return;
    }
    if (unitPage == UnitPage::routing)
    {
        routingPanel->setLayoutBounds(bounds, getMainScrollBarBounds().getHeight(),
                                      getLocalBounds().getCentreY() - bounds.getY());
        return;
    }

    slotsViewport.setBounds(bounds);
    const auto side = getSquareControlSize(*this);
    const auto count = static_cast<int>(hostSlotButtons.size());
    const auto step = side + getUiRowGap(*this);
    const auto height = count * step - getUiRowGap(*this);
    const auto width = bounds.getWidth() - (height > bounds.getHeight() ? slotsViewport.getScrollBarThickness() : 0);
    slotsContent.setSize(width, juce::jmax(height, bounds.getHeight()));
    const auto gap = getUiGap(*this);
    const auto labelX = side + gap;
    const auto labelRight = width - (height > bounds.getHeight() ? gap : 0);
    for (int index = 0; index < count; ++index)
    {
        const auto y = index * step;
        hostSlotNameFields[size_t(index)]->setBounds(0, y, side, side);
        hostSlotButtons[size_t(index)]->setBounds(labelX, y, labelRight - labelX, side);
    }
}

void AvaAudioProcessorEditor::layoutCrossoverSection(juce::Rectangle<int>& bounds)
{
    auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get());

    if (editor == nullptr)
        return;

    editor->getMainControls();
    editor->setInterceptsMouseClicks(false, true);
    editor->setBounds(bounds.withY(0).withHeight(getHeight()));
    editor->setVisible(true);

}

bool AvaAudioProcessorEditor::updateEditorResizeLimits()
{
    // Applying resize limits may synchronously call resized() again.
    if (updatingEditorResizeLimits)
        return false;

    const juce::ScopedValueSetter<bool> guard(updatingEditorResizeLimits, true);

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

    auto* constrainer = getConstrainer();
    if (constrainer == nullptr)
        return false;

    const auto width = getEditorWidth(*this);
    constrainer->setFixedAspectRatio(0.0);
    if (constrainer->getMinimumWidth() != width
        || constrainer->getMinimumHeight() != getMinimumEditorHeight()
        || constrainer->getMaximumWidth() != width
        || constrainer->getMaximumHeight() != getMaximumEditorHeight())
        setResizeLimits(width, getMinimumEditorHeight(), width, getMaximumEditorHeight());

    const auto height = juce::jlimit(getMinimumEditorHeight(), getMaximumEditorHeight(),
                                     getHeight() > 0 ? getHeight() : getEditorHeight(*this));
    if (width == getWidth() && height == getHeight()) return false;
    setSize(width, height);
    return true;
}

juce::Rectangle<int> AvaAudioProcessorEditor::getModuleViewportBounds() const
{
    return filterViewport.getBounds();
}

juce::Rectangle<int> AvaAudioProcessorEditor::getMainScrollBarBounds() const
{
    const auto gap = getUiGap(*this);
    const auto bottom = quickAccessStrip.getY();
    const auto scrollBottom = shell_editor_ui::dockControlCount(getRootWindowState()) == 0
        ? juce::jmin(bottom, dockClipIndicator.getY() - gap) : bottom;
    return {getWidth() - gap - editorScrollBarWidth, 2 * gap,
            editorScrollBarWidth, juce::jmax(0, scrollBottom - 2 * gap)};
}

void AvaAudioProcessorEditor::syncMainScrollBar()
{
    const auto position = filterViewport.getViewPositionY();
    if (position != mainScrollPosition)
    {
        mainScrollPosition = position;
        mainScrollBar.getProperties().set("scrollActiveUntil", juce::Time::getMillisecondCounterHiRes() + 50.0);
        mainScrollBar.repaint();
    }
    mainScrollBar.setBounds(getMainScrollBarBounds());
    const auto visible = filterViewport.getHeight();
    const auto total = moduleContent.getHeight();
    mainScrollBar.setRangeLimits(0.0, double(juce::jmax(visible, total)), juce::dontSendNotification);
    mainScrollBar.setCurrentRange(double(position), double(visible), juce::dontSendNotification);
    mainScrollBar.setVisible(visible > 0 && total > visible && !isDockOnly() && filterViewport.isVisible()
                            && !hasUnitPage() && activeInstanceEditor == nullptr);
}

HeaderControlRow::~HeaderControlRow()
{
    block.removeComponentListener(this);
    viewport.onViewChanged = {};
    viewport.setViewedComponent(nullptr, false);
    scrollbar.removeListener(this);
}

void HeaderControlRow::attach(const juce::String& name)
{
    viewport.setName(name + "-row");
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(false, false, false, false);
    viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    viewport.setWantsKeyboardFocus(false);
    block.addAndMakeVisible(viewport);
    viewport.onViewChanged = [this] { syncScrollBar(); };

    scrollbar.setName(name + "-scrollbar");
    scrollbar.setAutoHide(false);
    scrollbar.setWantsKeyboardFocus(false);
    scrollbar.setSingleStepSize(getUiGap(block));
    scrollbar.headerRegions = [this]
    {
        const auto header = scrollbar.getLocalBounds();
        const auto frame = std::find_if(block.controlBlockFrames.begin(), block.controlBlockFrames.end(),
            [this](const auto& item) { return item.title == title; });
        const auto& caption = frame != block.controlBlockFrames.end() && frame->displayTitle.isNotEmpty()
            ? frame->displayTitle : title;
        const bool hasBypass = frame != block.controlBlockFrames.end() && frame->colour == uiGreyDark
            && block.hasModuleNameplateBypass();
        return CursorDragScrollBar::HeaderRegions {getControlBlockTitleZone(header, caption),
            hasBypass ? moduleNameplateBypassBounds(header) : juce::Rectangle<int>{},
            block.isExpandedNameHover(title)};
    };
    scrollbar.addListener(this);
    block.addComponentListener(this);
    componentParentHierarchyChanged(block);
}

int HeaderControlRow::getPreferredHeight() const
{
    constexpr auto padding = controlBlockPadding + controlBlockFrameThickness;
    return block.isBlockCollapsed(title) ? controlBlockHeaderHeight
        : controlBlockHeaderHeight + 2 * padding + getSquareControlSize(block);
}

void HeaderControlRow::layout(juce::Rectangle<int> header, std::span<juce::Component* const> controls)
{
    block.setBounds(header.withHeight(getPreferredHeight()));
    block.controlBlockFrames = {{block.getLocalBounds(), title, uiGreyDark}};
    layoutContents(controls);
}

void HeaderControlRow::layoutContents(std::span<juce::Component* const> controls)
{
    const auto collapsed = block.isBlockCollapsed(title);
    const auto side = getSquareControlSize(block);
    const auto gap = getUiGap(block);
    constexpr auto padding = controlBlockPadding + controlBlockFrameThickness;
    const auto step = side + gap;
    content.setSize(juce::jmax(0, int(controls.size()) * step - gap), side);
    for (int index = content.getNumChildComponents(); --index >= 0;)
        if (std::find(controls.begin(), controls.end(), content.getChildComponent(index)) == controls.end())
            content.removeChildComponent(index);
    std::vector<juce::Rectangle<int>> emptySlots;
    for (size_t index = 0; index < controls.size(); ++index)
    {
        auto* control = controls[index];
        const juce::Rectangle<int> bounds {int(index) * step, 0, side, side};
        if (control == nullptr)
        {
            emptySlots.push_back(bounds);
            continue;
        }
        if (control->getParentComponent() != &content) content.addAndMakeVisible(*control);
        control->setBounds(bounds);
    }
    content.setEmptySlots(std::move(emptySlots));
    const auto frame = getFrameBounds();
    viewport.setBounds(frame.getX() + padding, frame.getY() + controlBlockHeaderHeight + padding,
                       juce::jmax(0, frame.getWidth() - 2 * padding), side);
    viewport.setVisible(!collapsed);
    componentMovedOrResized(block, true, false);
    syncScrollBar();
}

void HeaderControlRow::syncScrollBar()
{
    const auto visible = viewport.getWidth();
    const auto total = content.getWidth();
    const auto position = viewport.getViewPositionX();
    if (juce::roundToInt(scrollbar.getCurrentRangeStart()) != position)
    {
        scrollbar.getProperties().set("scrollActiveUntil", juce::Time::getMillisecondCounterHiRes() + 50.0);
        scrollbar.repaint();
    }
    scrollbar.setRangeLimits(0.0, double(juce::jmax(visible, total)), juce::dontSendNotification);
    scrollbar.setCurrentRange(double(position), double(visible), juce::dontSendNotification);
    const auto showScrollbar = block.isVisible() && !block.isBlockCollapsed(title) && total > visible && visible > 0;
    block.getProperties().set("headerScrollbarVisible", showScrollbar);
    scrollbar.setVisible(showScrollbar);
}

juce::Rectangle<int> HeaderControlRow::getFrameBounds() const
{
    const auto frame = std::find_if(block.controlBlockFrames.begin(), block.controlBlockFrames.end(),
        [this](const auto& item) { return item.title == title; });
    return frame != block.controlBlockFrames.end() ? frame->bounds : block.getLocalBounds();
}

void HeaderControlRow::componentMovedOrResized(juce::Component&, bool moved, bool)
{
    if (moved)
        scrollbar.setBounds(getFrameBounds().withHeight(controlBlockHeaderHeight)
                                .translated(block.getX(), block.getY()));
}

void HeaderControlRow::componentParentHierarchyChanged(juce::Component&)
{
    auto* parent = block.getParentComponent();
    if (parent == scrollbar.getParentComponent()) return;
    // The block paints its header over its children, so the scrollbar is a sibling.
    if (parent != nullptr)
    {
        parent->addChildComponent(scrollbar);
        componentMovedOrResized(block, true, false);
    }
    else if (auto* previous = scrollbar.getParentComponent()) previous->removeChildComponent(&scrollbar);
    syncScrollBar();
}

void HeaderControlRow::componentVisibilityChanged(juce::Component&)
{
    syncScrollBar();
}

void HeaderControlRow::scrollBarMoved(juce::ScrollBar*, double position)
{
    viewport.setViewPosition(juce::roundToInt(position), 0);
}

void AvaAudioProcessorEditor::scrollBarMoved(juce::ScrollBar* bar, double position)
{
    if (bar == &mainScrollBar)
        filterViewport.setViewPosition(filterViewport.getViewPositionX(), juce::roundToInt(position));
}

bool AvaAudioProcessorEditor::isDockOnly() const
{
    const auto& state = getRootWindowState();
    return shell_editor_ui::dockControlCount(state) > 0
        && bool(state.getProperty(shell_editor_ui::key("dock_only"), false));
}

int AvaAudioProcessorEditor::getMinimumEditorHeight() const
{
    return isDockOnly() ? (bool(getRootWindowState().getProperty(shell_editor_ui::key("dock_vertical"), false)) ? getDockLength(true, shell_editor_ui::dockControlCount(getRootWindowState())) : getSquareControlSize(*this) + 3 * getUiGap(*this)) : getEditorWidth(*this);
}

int AvaAudioProcessorEditor::getMaximumEditorHeight() const
{
    return isDockOnly() ? getMinimumEditorHeight() : maximumEditorHeight;
}

void AvaAudioProcessorEditor::applyDockVisibility()
{
    dockCollapseToggle.setToggleState(isDockOnly(), juce::dontSendNotification);
    dockVerticalToggle.setToggleState(bool(getRootWindowState().getProperty(shell_editor_ui::key("dock_vertical"), false)), juce::dontSendNotification);
    const auto dockOnly = isDockOnly();
    const auto* visibleEditor = activeInstanceEditor != nullptr ? activeInstanceEditor.get() : this;
    const auto showMainControls = dockOnly || !visibleEditor->hasUnitPage();
    const auto showDock = showMainControls && shell_editor_ui::dockControlCount(getRootWindowState()) > 0;
    for (juce::Component* component : std::array<juce::Component*, 4>{
             &quickAccessStrip, &dockSortToggle, &dockCollapseToggle, &dockVerticalToggle})
        component->setVisible(showDock);
    dockSortOverlay.setVisible(showDock && dockSortToggle.getToggleState());
    dockClipIndicator.setVisible(showMainControls);
    if (!dockOnly) return;
    filterViewport.setVisible(false);
    if (controlSearchPanel != nullptr) controlSearchPanel->setVisible(false);
    pinnedRoutingViewport.setVisible(false);
    if (aboutPanel != nullptr) aboutPanel->setVisible(false);
    if (routingPanel != nullptr) routingPanel->setVisible(false);
    slotsViewport.setVisible(false);
    if (crossoverEditor != nullptr) crossoverEditor->setVisible(false);
    if (blockChainModuleEditor != nullptr) blockChainModuleEditor->setVisible(false);
    if (dynModuleEditor != nullptr) dynModuleEditor->setVisible(false);
    mainScrollBar.setVisible(false);
}
