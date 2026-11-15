#include "Controls.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include "Prompt.h"

#include <utility>

void ControlBlockComponent::bindCollapseState(juce::AudioProcessorValueTreeState& state,
                                              juce::String scope)
{
    collapseState = &state;
    collapseScope = std::move(scope);
}

juce::Identifier ControlBlockComponent::collapseProperty(const juce::String& title) const
{
    // All block titles are ASCII. Do not use the component's address or index
    // in the key: sorting/rebinding must not change the identity.
    auto name = title.toLowerCase().replaceCharacter(' ', '-').replaceCharacter('/', '-')
                     .replace("+", "plus");
    return juce::Identifier("ava.editor.ui.collapsed." + collapseScope + "." + name);
}

bool ControlBlockComponent::isBlockCollapsed(const juce::String& title) const
{
    if (!collapsible) return false;
    const auto defaultCollapsed = collapsedBlockTitles.contains(title);
    if (collapseState == nullptr || collapseScope.isEmpty()) return defaultCollapsed;
    return bool(collapseState->state.getProperty(collapseProperty(title), defaultCollapsed));
}

void ControlBlockComponent::setBlockCollapsed(const juce::String& title, const bool collapsed)
{
    if (!collapsible || title.isEmpty() || isBlockCollapsed(title) == collapsed)
        return;
    if (collapseState != nullptr && collapseScope.isNotEmpty())
        collapseState->state.setProperty(collapseProperty(title), collapsed, nullptr);
    else if (collapsed)
        collapsedBlockTitles.addIfNotAlreadyThere(title);
    else
        collapsedBlockTitles.removeString(title);
    repaint();
}

bool ControlBlockComponent::expandAllBlocks()
{
    juce::StringArray titles = collapsedBlockTitles;
    for (const auto& frame : controlBlockFrames) titles.addIfNotAlreadyThere(frame.title);
    bool changed = false;
    for (const auto& title : titles)
        if (isBlockCollapsed(title))
        {
            setBlockCollapsed(title, false);
            changed = true;
        }
    return changed;
}

bool ControlBlockComponent::collapseAllBlocks()
{
    juce::StringArray titles = collapsedBlockTitles;
    for (const auto& frame : controlBlockFrames) titles.addIfNotAlreadyThere(frame.title);
    bool changed = false;
    for (const auto& title : titles)
        if (!isBlockCollapsed(title))
        {
            setBlockCollapsed(title, true);
            changed = true;
        }
    return changed;
}

juce::String ControlBlockComponent::blockHeaderAt(juce::Point<int> position) const
{
    // Header hover/click is disabled only for non-collapsible pages.
    if (!collapsible) return {};
    for (auto it = controlBlockFrames.rbegin(); it != controlBlockFrames.rend(); ++it)
        if (it->bounds.withHeight(controlBlockHeaderHeight).contains(position))
            return it->title;
    return {};
}

void ControlBlockComponent::updateHoveredBlock(juce::Point<int> position)
{
    const auto title = blockHeaderAt(position);
    if (hoveredBlockTitle == title) return;
    hoveredBlockTitle = title;
    hoveredBlockVisualReady = false;
    const auto generation = ++hoverGeneration;
    if (title.isNotEmpty())
        juce::Timer::callAfterDelay(50,
            [safe = juce::Component::SafePointer<ControlBlockComponent>(this), title, generation]
            {
                if (safe == nullptr || safe->hoverGeneration != generation
                    || safe->hoveredBlockTitle != title) return;
                safe->hoveredBlockVisualReady = true;
                safe->repaint();
            });
    repaint();
}

void ControlBlockComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu()) return;
    // A click on a block's background/header is a neutral focus target.
    // Keep Enter routed to the editor instead of leaving the host focused.
    if (auto* editor = findParentComponentOfClass<AvaAudioProcessorEditor>())
        editor->grabKeyboardFocus();
}

void ControlBlockComponent::mouseMove(const juce::MouseEvent& event)
{
    updateHoveredBlock(event.getPosition());
}

void ControlBlockComponent::mouseEnter(const juce::MouseEvent& event)
{
    updateHoveredBlock(event.getPosition());
}

void ControlBlockComponent::mouseExit(const juce::MouseEvent&)
{
    if (hoveredBlockTitle.isEmpty()) return;
    hoveredBlockTitle.clear();
    hoveredBlockVisualReady = false;
    ++hoverGeneration;
    repaint();
}

juce::MouseCursor ControlBlockComponent::getMouseCursor()
{
    // Collapsible module headers remain clickable but use the normal cursor.
    return juce::MouseCursor::NormalCursor;
}

void ControlBlockComponent::mouseUp(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || event.mouseWasDraggedSinceMouseDown()) return;
    const auto title = blockHeaderAt(event.getPosition());
    if (title.isEmpty()) return;
    setBlockCollapsed(title, !isBlockCollapsed(title));
    // Reflow the entire shared block page; it recalculates every frame height
    // and preserves scroll position when the section is collapsed or expanded.
    if (auto* editor = findParentComponentOfClass<AvaAudioProcessorEditor>()) editor->resized();
    else resized();
    repaint();
}

bool isManualControlSelected(const juce::Component& component)
{
    // The docking selection highlight belongs to the source in the main
    // viewport, not to its mirrored quick-access control. Ctrl+click on a
    // mirror still uses the same saved selection key to unpin it.
    for (auto* node = &component; node != nullptr; node = node->getParentComponent())
        if (bool(node->getProperties().getWithDefault("suppressManualSelectionHighlight", false)))
            return false;
    if (const auto* editor = component.findParentComponentOfClass<AvaAudioProcessorEditor>())
        return editor->isControlManuallySelected(component);
    return false;
}

bool ControlSelectionGesture::begin(juce::Component& source, const juce::MouseEvent& event)
{
    target = nullptr;
    // macOS may report Ctrl+click as a right-button event. Dock selection
    // is independent of whether the parameter can currently be edited.
    if (!(event.mods.isLeftButtonDown() || event.mods.isRightButtonDown())
        || !event.mods.isCtrlDown() || event.mods.isShiftDown()
        || event.mods.isAltDown() || event.mods.isCommandDown()) return false;
    auto* control = &source;
    if (!bool(source.getProperties().getWithDefault("independentDockSelection", false)))
        if (auto* field = source.findParentComponentOfClass<ParameterField>()) control = field;
    target = control;
    return true;
}

bool ControlSelectionGesture::finish(const juce::MouseEvent& event)
{
    auto* control = target.getComponent();
    if (control == nullptr) return false;
    target = nullptr;
    if (!event.mouseWasDraggedSinceMouseDown()
        && control->getLocalBounds().contains(event.getEventRelativeTo(control).getPosition()))
    {
        if (auto* editor = control->findParentComponentOfClass<AvaAudioProcessorEditor>())
        {
            if (!(editor->isDockOnly() && control->getProperties().contains("quickAccessSelectionKey")))
                editor->toggleControlManualSelection(*control);
        }
        repaintValueOverlay(*control);
    }
    return true;
}

CopyPasteTextEditor::CopyPasteTextEditor()
{
    setInputFilter(new KeyboardLayoutEditorInputFilter(), true);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    // TextEditor reserves two pixels on the right for the caret.
    // Match that on the left so centred text stays centred when editing.
    setIndents(2, 0);
    setBorder(juce::BorderSize<int>(1));
    setColour(juce::CaretComponent::caretColourId, juce::Colour(0xff222222));
    // Paint the opaque selection below the glyphs ourselves: JUCE halves
    // the native selection opacity whenever the editor loses keyboard focus.
    setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::highlightedTextColourId, uiWhite);
}

CopyPasteTextEditor::~CopyPasteTextEditor() = default;

bool CopyPasteTextEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.isKeyCode(juce::KeyPress::escapeKey)) return true;
    return juce::TextEditor::keyPressed(key);
}

void CopyPasteTextEditor::paint(juce::Graphics& g)
{
    const auto background = findColour(juce::TextEditor::backgroundColourId);
    if (bool(getProperties().getWithDefault("controlTextButtonEditor", false)))
    {
        paintControlFill(g, getLocalBounds(), uiGreyDark);
        paintControlTextButton(g, getControlTextBounds(*this, getLocalBounds()), false, true);
    }
    else
        paintControlFill(g, getLocalBounds(), background);

    const auto selection = getHighlightedRegion();
    if (!selection.isEmpty())
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.setColour(juce::Colour(0xff222222));
        auto clip = getLocalBounds();
        for (auto* child : getChildren())
            if (auto* viewport = dynamic_cast<juce::Viewport*>(child))
                clip = getLocalArea(viewport, viewport->getLocalBounds());
        g.reduceClipRegion(clip);
        for (const auto& area : getTextBounds(selection)) g.fillRect(area);
    }
}

void CopyPasteTextEditor::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isCtrlDown() && !event.mods.isRightButtonDown()) return;
    if (!isPopupMenuEnabled() || !event.mods.isPopupMenu() || getParentComponent() == nullptr)
    {
        juce::TextEditor::mouseDown(event);
        return;
    }

    auto* scope = static_cast<juce::Component*>(findParentComponentOfClass<PromptComponent>());
    if (scope == nullptr) scope = findParentComponentOfClass<juce::AudioProcessorEditor>();
    if (scope == nullptr) scope = getTopLevelComponent();
    contextMenu.reset();
    const auto selection = getHighlightedRegion();
    const auto compactSize = getCompactControlSize(*this);
    auto anchor = juce::Rectangle<int>(2 * compactSize + getUiGap(*this), compactSize);
    anchor.setCentre(scope->getLocalPoint(this, event.getPosition()));
    auto popup = makeChoicePrompt(anchor, { "COPY", "PASTE" }, -1,
        { !selection.isEmpty(), !isReadOnly() }, juce::Justification::centred,
        [safeThis = juce::Component::SafePointer<CopyPasteTextEditor>(this), selection](int choice)
        {
            if (safeThis == nullptr) return;
            safeThis->setHighlightedRegion(selection);
            safeThis->performPopupMenuAction(choice == 0 ? juce::StandardApplicationCommandIDs::copy
                                                        : juce::StandardApplicationCommandIDs::paste);
            if (safeThis->isShowing()) safeThis->grabKeyboardFocus();
        }, {},
        [safeThis = juce::Component::SafePointer<CopyPasteTextEditor>(this)]
        {
            if (safeThis == nullptr) return;
            safeThis->contextMenu.reset();
            if (safeThis->isShowing()) safeThis->grabKeyboardFocus();
        }, false, {}, true);
    popup->setName("text-edit-context-menu");
    scope->addAndMakeVisible(*popup);
    popup->setBounds(scope->getLocalBounds());
    popup->toFront(false);
    contextMenu = std::move(popup);
    if (isCurrentlyModal()) contextMenu->enterModalState(false, nullptr, false);
}

void CopyPasteTextEditor::addPopupMenuItems(juce::PopupMenu& menuToAddTo, const juce::MouseEvent*)
{
    const auto hasSelection = ! getHighlightedRegion().isEmpty();
    const auto canPaste = ! isReadOnly();

    menuToAddTo.addItem(juce::StandardApplicationCommandIDs::copy, "COPY", hasSelection, false);
    menuToAddTo.addItem(juce::StandardApplicationCommandIDs::paste, "PASTE", canPaste, false);
}

void CopyPasteTextEditor::performPopupMenuAction(const int menuItemID)
{
    if (menuItemID == juce::StandardApplicationCommandIDs::copy)
    {
        copyToClipboard();
        return;
    }

    if (menuItemID == juce::StandardApplicationCommandIDs::paste)
        pasteFromClipboard();
}
