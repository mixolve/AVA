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
    const auto modulePageVisible = !aboutExpanded;
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
            const auto clip = filterViewport.getBounds();
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
    if (crossoverEditor != nullptr && !aboutExpanded)
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
        || aboutControl == nullptr)
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
    if (aboutExpanded)
    {
        if (aboutPanel != nullptr) aboutPanel->setBounds(bounds.withBottom(getHeight()));
        finalizeLayout();
        return;
    }
    if (aboutPanel != nullptr) aboutPanel->setBounds({});
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

void AvaAudioProcessorEditor::syncMainScrollBar()
{
    const auto position = filterViewport.getViewPositionY();
    if (position != mainScrollPosition)
    {
        mainScrollPosition = position;
        mainScrollBar.getProperties().set("scrollActiveUntil", juce::Time::getMillisecondCounterHiRes() + 50.0);
        mainScrollBar.repaint();
    }
    const auto gap = getUiGap(*this);
    const auto scrollBottom = shell_editor_ui::dockControlCount(getRootWindowState()) == 0
        ? juce::jmin(filterViewport.getBottom(), dockClipIndicator.getY() - gap) : filterViewport.getBottom();
    mainScrollBar.setBounds(getWidth() - gap - editorScrollBarWidth, 2 * gap,
                            editorScrollBarWidth, juce::jmax(0, scrollBottom - 2 * gap));
    mainScrollBar.setVisible(!isDockOnly() && filterViewport.isVisible() && !aboutExpanded
                            && activeInstanceEditor == nullptr);
    const auto visible = filterViewport.getHeight();
    mainScrollBar.setRangeLimits(0.0, double(juce::jmax(visible, moduleContent.getHeight())), juce::dontSendNotification);
    mainScrollBar.setCurrentRange(double(filterViewport.getViewPositionY()), double(visible), juce::dontSendNotification);
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
    if (!isDockOnly()) return;
    filterViewport.setVisible(false);
    if (controlSearchPanel != nullptr) controlSearchPanel->setVisible(false);
    pinnedRoutingViewport.setVisible(false);
    if (aboutPanel != nullptr) aboutPanel->setVisible(false);
    if (crossoverEditor != nullptr) crossoverEditor->setVisible(false);
    if (blockChainModuleEditor != nullptr) blockChainModuleEditor->setVisible(false);
    if (dynModuleEditor != nullptr) dynModuleEditor->setVisible(false);
    mainScrollBar.setVisible(false);
}
