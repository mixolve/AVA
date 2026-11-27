#include "Editor.h"
#include "ChoiceControl.h"
#include "FilterSection.h"
#include "../crossover/Editor.h"
#include "Metrics.h"
#include "ModuleList.h"
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
    if (activeInstanceEditor != nullptr)
    {
        paintOuterFrame();
        paintControlHelpOverlay(g);
        return;
    }
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
    paintControlHelpOverlay(g);
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

void AvaAudioProcessorEditor::raisePinnedControls()
{
    for (auto* component : std::array<juce::Component*, 4>{
             controlSearchPanel.get(), &pinnedRoutingViewport,
             pinnedRoutingScrollControl.get(), pinnedVerticalScrollControl.get()})
    {
        if (component == nullptr || !component->isVisible()) continue;
        if (textPromptOverlay != nullptr && textPromptOverlay->isVisible()
            && textPromptOverlay->getParentComponent() == this)
            component->toBehind(textPromptOverlay.get());
        else
            component->toFront(false);
    }
}

void AvaAudioProcessorEditor::finalizeLayout() noexcept
{
    applyPromptVisibility();
    applyDockVisibility();
    syncVerticalScrollControl();
    // Keep the fixed quick-access row in front of the scrolling page but
    // behind prompts and the active nested editor.
    quickAccessStrip.toFront(false);
    dockSortOverlay.toFront(false);
    updatePinnedSearchPosition();
    // Clear the previous positions when controls move or pages change.
    repaint();
    if (crossoverEditor != nullptr && !hasUnitPage())
        crossoverEditor->toFront(false);
    if (activeInstanceEditor != nullptr)
    {
        activeInstanceEditor->setBounds(getLocalBounds());
        activeInstanceEditor->toFront(false);
    }
    raisePinnedControls();
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
    bounds.removeFromRight(getUiGap(*this));

    bounds.removeFromBottom(editorInsetBottom);
    bounds.removeFromTop(editorInsetTop);

    layoutGlobalSections(bounds);
    layoutQuickAccessStrip();
    const auto canvas = getLocalBounds();
    bounds = bounds.withY(canvas.getY() + editorInsetTop)
        .withHeight(canvas.getHeight() - editorInsetTop - editorInsetBottom);
    if (hasUnitPage())
    {
        const auto popupBounds = canvas.withTrimmedTop(editorInsetTop)
            .withTrimmedBottom(getUiGap(*this)).reduced(getUiGap(*this), 0);
        layoutUnitPage(popupBounds);
        finalizeLayout();
        return;
    }
    bounds.setRight(canvas.getRight() - getUiGap(*this));
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
    const auto dockBottomLimit = quickAccessStrip.getY();
    bounds.setBottom(juce::jmax(bounds.getY(), juce::jmin(bounds.getBottom(), dockBottomLimit)));
    if (unitPage == UnitPage::about)
    {
        if (aboutPanel != nullptr) aboutPanel->setBounds(bounds);
        return;
    }
    if (unitPage == UnitPage::routing)
    {
        routingPanel->setLayoutBounds(bounds, bounds.getHeight(),
                                      getLocalBounds().getCentreY() - bounds.getY());
        return;
    }

    const auto side = getSquareControlSize(*this);
    const auto gap = getUiGap(*this);
    const auto scrollRow = bounds.removeFromTop(juce::jmin(side, bounds.getHeight()));
    slotsVerticalScrollControl->setBounds(scrollRow.withTrimmedLeft(juce::jmax(0, scrollRow.getWidth() - side)));
    bounds.removeFromTop(juce::jmin(gap, bounds.getHeight()));
    slotsViewport.setBounds(bounds);
    const auto count = static_cast<int>(hostSlotButtons.size());
    const auto step = side + getUiRowGap(*this);
    const auto height = count * step - getUiRowGap(*this);
    const auto width = bounds.getWidth();
    slotsContent.setSize(width, juce::jmax(height, bounds.getHeight()));
    const auto labelX = side + gap;
    syncSlotsVerticalScrollControl();
    for (int index = 0; index < count; ++index)
    {
        const auto y = index * step;
        hostSlotNameFields[size_t(index)]->setBounds(0, y, side, side);
        hostSlotButtons[size_t(index)]->setBounds(labelX, y, juce::jmax(0, width - labelX), side);
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

namespace
{
void syncVerticalControl(LocalContinuousControl& control, const UiScrollViewport& viewport)
{
    const auto* content = viewport.getViewedComponent();
    const auto maximum = content != nullptr ? juce::jmax(0, content->getHeight() - viewport.getHeight()) : 0;
    const auto position = juce::jlimit(0, maximum, viewport.getViewPositionY());
    control.setValue(maximum > 0 ? double(position) / double(maximum) : 0.0, false);
    control.setInteractionEnabled(control.isVisible() && viewport.isVisible() && maximum > 0);
}
}

void AvaAudioProcessorEditor::syncVerticalScrollControl()
{
    if (pinnedVerticalScrollControl == nullptr) return;
    const auto* visibleEditor = activeInstanceEditor != nullptr ? activeInstanceEditor.get() : this;
    syncVerticalControl(*pinnedVerticalScrollControl, visibleEditor->filterViewport);
}

void AvaAudioProcessorEditor::syncSlotsVerticalScrollControl()
{
    if (slotsVerticalScrollControl != nullptr)
        syncVerticalControl(*slotsVerticalScrollControl, slotsViewport);
}

HeaderControlRow::HeaderControlRow(ControlBlockComponent& blockIn)
    : block(blockIn)
{
}

HeaderControlRow::~HeaderControlRow()
{
    viewport.onViewChanged = {};
    viewport.setViewedComponent(nullptr, false);
}

void HeaderControlRow::attach(const juce::String& name)
{
    viewport.setName(name + "-row");
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(false, false, false, false);
    viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    viewport.setWantsKeyboardFocus(false);
    block.addAndMakeVisible(viewport);
    viewport.onViewChanged = [this] { syncScrollControl(); };

    // A permanently pinned, non-wrapping continuous scroll tile (00–99).
    // It occupies the first slot even when the row fits the viewport.
    scrollControl = std::make_unique<LocalContinuousControl>("SCROLL");
    scrollControl->setName("scroll");
    scrollControl->setComponentID(name + "-scroll");
    scrollControl->getProperties().set("disableDockSelection", true);
    scrollControl->onValueChanged = [this] { scrollToPosition(); };
    block.addAndMakeVisible(*scrollControl);
}

void HeaderControlRow::setScrollLabel(const juce::String& label)
{
    if (scrollControl == nullptr) return;
    scrollControl->setTitleText(label);
    scrollControl->setName(label.replaceCharacter('\n', ' ').toLowerCase());
}

void HeaderControlRow::setModuleBypass(const std::function<bool()>& isBypassed,
                                       const std::function<void()>& toggle)
{
    if (scrollControl != nullptr)
        scrollControl->setBottomActions({{"BP", toggle, isBypassed}});
}

void HeaderControlRow::enableModuleClipboard()
{
    copyButton = std::make_unique<PushControl>("COPY");
    pasteButton = std::make_unique<PushControl>("PASTE");
    copyButton->setComponentID("module-copy-settings");
    pasteButton->setComponentID("module-paste-settings");
    copyButton->setBottomIndicatorsVisible(false, false);
    pasteButton->setBottomIndicatorsVisible(false, false);
}

void HeaderControlRow::setModuleClipboardActions(const std::function<void()>& copy,
                                                const std::function<void()>& paste)
{
    copyButton->onTrigger = copy;
    pasteButton->onTrigger = paste;
}

void HeaderControlRow::layoutModuleControls(juce::Rectangle<int> bounds,
                                          std::vector<juce::Component*>& controls,
                                          std::span<const int> columnSpans)
{
    const std::array divider {int(controls.size())};
    controls.push_back(copyButton.get());
    controls.push_back(pasteButton.get());
    layoutInline(bounds, controls, true, columnSpans, divider);
}

void HeaderControlRow::layoutInline(juce::Rectangle<int> bounds,
                                    std::span<juce::Component* const> controls,
                                    bool visible, std::span<const int> columnSpans,
                                    std::span<const int> groupBreaks)
{
    layoutRow(bounds, controls, visible, columnSpans, groupBreaks);
}

void HeaderControlRow::layoutRow(juce::Rectangle<int> bounds,
                                 std::span<juce::Component* const> controls,
                                 bool visible, std::span<const int> columnSpans,
                                 std::span<const int> groupBreaks)
{
    const auto side = getSquareControlSize(block);
    const auto gap = getUiGap(block);
    const auto step = side + gap;
    int contentWidth = 0;
    for (size_t index = 0; index < controls.size(); ++index)
    {
        contentWidth += juce::jmax(1, index < columnSpans.size() ? columnSpans[index] : 1) * step;
    }
    content.setSize(juce::jmax(0, contentWidth - (controls.empty() ? 0 : gap)), side);
    for (int index = content.getNumChildComponents(); --index >= 0;)
        if (std::find(controls.begin(), controls.end(), content.getChildComponent(index)) == controls.end())
            content.removeChildComponent(index);
    std::vector<juce::Rectangle<int>> emptySlots;
    std::vector<juce::Rectangle<int>> separators;
    int x = 0;
    for (size_t index = 0; index < controls.size(); ++index)
    {
        if (index > 0 && std::find(groupBreaks.begin(), groupBreaks.end(), int(index)) != groupBreaks.end())
            separators.emplace_back(x - (gap + 2) / 2, 0, 2, side);
        const int columns = juce::jmax(1, index < columnSpans.size() ? columnSpans[index] : 1);
        const juce::Rectangle<int> cell {x, 0, columns * step - gap, side};
        auto* control = controls[index];
        if (control == nullptr) emptySlots.push_back(cell);
        else
        {
            if (control->getParentComponent() != &content) content.addAndMakeVisible(*control);
            control->setBounds(cell);
        }
        x += columns * step;
    }
    content.setEmptySlots(std::move(emptySlots));
    content.setGroupSeparators(std::move(separators));
    // The first tile belongs to SCROLL unconditionally. It remains visible,
    // disabled when the following viewport has no horizontal overflow.
    scrollControl->setBounds(bounds.getX(), bounds.getY(), side, side);
    scrollControl->setVisible(visible);
    viewport.setBounds(bounds.getX() + step, bounds.getY(),
        juce::jmax(0, bounds.getWidth() - step), side);
    viewport.setVisible(visible);
    if (content.getWidth() <= viewport.getWidth()) viewport.setViewPosition(0, 0);
    syncScrollControl();
    if (visible)
    {
        viewport.toFront(false);
        scrollControl->toFront(false);
    }
}

void HeaderControlRow::scrollToPosition()
{
    const auto maximum = juce::jmax(0, content.getWidth() - viewport.getWidth());
    viewport.setViewPosition(juce::roundToInt(scrollControl->getValue() * double(maximum)), 0);
}

void HeaderControlRow::syncScrollControl()
{
    if (scrollControl == nullptr) return;
    const auto maximum = juce::jmax(0, content.getWidth() - viewport.getWidth());
    const auto position = juce::jlimit(0, maximum, viewport.getViewPositionX());
    scrollControl->setValue(maximum > 0 ? double(position) / double(maximum) : 0.0, false);
    scrollControl->setInteractionEnabled(scrollControl->isVisible() && maximum > 0);
}

bool AvaAudioProcessorEditor::isDockOnly() const
{
    const auto& state = getRootWindowState();
    return shell_editor_ui::dockControlCount(state) > 0
        && bool(state.getProperty(shell_editor_ui::key("dock_only"), false));
}

int AvaAudioProcessorEditor::getMainViewportTop(int searchHeight) const noexcept
{
    // Search, then the fixed INSTS row, each followed by the standard gap.
    return searchHeight + getSquareControlSize(*this) + 4 * getUiGap(*this);
}

int AvaAudioProcessorEditor::getDockStripHeight() const noexcept
{
    return getSquareControlSize(*this) + 2 * getUiGap(*this);
}

int AvaAudioProcessorEditor::getExpandedMinimumEditorHeight() const
{
    const auto gap = getUiGap(*this);
    const auto rowStep = getSquareControlSize(*this) + gap;
    // UNIT and INSTANCE, the two module-list rows, then space for one more tile row.
    const auto mainHeight = 3 * rowStep + ModuleList::getContentHeight(*this);
    // Use the main page's dock height even while ROUTING or SLOTS is open.
    const auto bottomHeight = shell_editor_ui::dockControlCount(getRootWindowState()) > 0
        ? getDockStripHeight() : gap;
    return getMainViewportTop(controlBlockHeaderHeight) + mainHeight + bottomHeight;
}

int AvaAudioProcessorEditor::getMinimumEditorHeight() const
{
    if (!isDockOnly()) return getExpandedMinimumEditorHeight();
    const auto& state = getRootWindowState();
    return bool(state.getProperty(shell_editor_ui::key("dock_vertical"), false))
        ? getDockLength(true, shell_editor_ui::dockControlCount(state))
        : getSquareControlSize(*this) + 3 * getUiGap(*this);
}

int AvaAudioProcessorEditor::getMaximumEditorHeight() const
{
    return isDockOnly() ? getMinimumEditorHeight() : maximumEditorHeight;
}

bool AvaAudioProcessorEditor::shouldShowDock() const
{
    return unitPage != UnitPage::routing && unitPage != UnitPage::slots
        && shell_editor_ui::dockControlCount(getRootWindowState()) > 0;
}

void AvaAudioProcessorEditor::applyDockVisibility()
{
    dockControls.collapse.setToggleState(isDockOnly(), juce::dontSendNotification);
    dockControls.vertical.setToggleState(bool(getRootWindowState().getProperty(shell_editor_ui::key("dock_vertical"), false)), juce::dontSendNotification);
    const auto dockOnly = isDockOnly();
    const auto showDock = shouldShowDock();
    quickAccessStrip.setVisible(showDock);
    dockSortOverlay.setVisible(showDock && dockControls.sort.getToggleState());
    if (quickAccessScrollControl != nullptr) quickAccessScrollControl->setVisible(showDock);
    if (!dockOnly) return;
    filterViewport.setVisible(false);
    if (controlSearchPanel != nullptr) controlSearchPanel->setVisible(false);
    pinnedRoutingViewport.setVisible(false);
    if (pinnedRoutingScrollControl != nullptr) pinnedRoutingScrollControl->setVisible(false);
    if (pinnedVerticalScrollControl != nullptr) pinnedVerticalScrollControl->setVisible(false);
    if (aboutPanel != nullptr) aboutPanel->setVisible(false);
    if (routingPanel != nullptr) routingPanel->setVisible(false);
    slotsViewport.setVisible(false);
    if (slotsVerticalScrollControl != nullptr) slotsVerticalScrollControl->setVisible(false);
    if (crossoverEditor != nullptr) crossoverEditor->setVisible(false);
    if (blockChainModuleEditor != nullptr) blockChainModuleEditor->setVisible(false);
    if (dynModuleEditor != nullptr) dynModuleEditor->setVisible(false);
    if (pinnedVerticalScrollControl != nullptr) pinnedVerticalScrollControl->setVisible(false);
}
