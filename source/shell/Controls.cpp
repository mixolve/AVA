#include "Controls.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"
#include "Prompt.h"

#include <utility>

void FlatScrollBarLookAndFeel::drawScrollbar(juce::Graphics& graphics, juce::ScrollBar& scrollbar,
                                            int x, int y, int width, int height, bool vertical,
                                            int thumbStart, int thumbSize, bool hovered, bool pressed)
{
    juce::ignoreUnused(scrollbar, hovered, pressed);
    graphics.setColour(uiWhite);
    if (vertical)
        graphics.fillRect(x, thumbStart, width, thumbSize);
    else
        graphics.fillRect(thumbStart, y, thumbSize, height);
}

CopyPasteTextEditor::CopyPasteTextEditor()
{
    setInputFilter(new UppercaseEditorInputFilter(), true);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    // TextEditor reserves two pixels on the right for the caret.
    // Match that on the left so centred text stays centred when editing.
    setIndents(2, 0);
    setBorder(juce::BorderSize<int>(1));
    setColour(juce::CaretComponent::caretColourId, uiWhite);
    setColour(juce::TextEditor::highlightColourId, uiGreyLight);
    setColour(juce::TextEditor::highlightedTextColourId, uiBlack);
}

CopyPasteTextEditor::~CopyPasteTextEditor()
{
    contextMenu.reset();
}

void CopyPasteTextEditor::mouseDown(const juce::MouseEvent& event)
{
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
    auto anchor = juce::Rectangle<int>(juce::jmax(96, getWidth()), rowHeight);
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
        });
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

NoTickComboBox::NoTickComboBox()
{
    setScrollWheelEnabled(false);
}

void NoTickComboBox::showPopup()
{
    if (! isEnabled())
        return;

    if (promptStylePopupEnabled)
    {
        const auto itemCount = getNumItems();

        if (itemCount > 0)
        {
            juce::StringArray itemTexts;
            std::vector<bool> itemEnabledStates;
            itemTexts.ensureStorageAllocated(itemCount);
            itemEnabledStates.reserve(static_cast<size_t>(itemCount));

            for (int index = 0; index < itemCount; ++index)
            {
                itemTexts.add(getItemText(index));
                itemEnabledStates.push_back(isItemEnabled(getItemId(index)));
            }

            const auto currentSelectedItemIndex = getSelectedItemIndex();
            auto onSelect = [safePointer = juce::Component::SafePointer<NoTickComboBox>(this), currentSelectedItemIndex] (int selectedIndex)
            {
                if (safePointer == nullptr)
                    return;

                const auto shouldReSelectCurrentItem = selectedIndex == currentSelectedItemIndex;
                safePointer->setSelectedItemIndex(selectedIndex, juce::sendNotificationSync);

                if (shouldReSelectCurrentItem && safePointer->onReselectedCurrentItem != nullptr)
                    safePointer->onReselectedCurrentItem();

                if (auto* handler = safePointer->getAccessibilityHandler())
                    handler->grabFocus();
            };

            if (choicePromptPresenter != nullptr)
            {
                choicePromptPresenter(itemTexts,
                                      currentSelectedItemIndex,
                                      std::move(itemEnabledStates),
                                      popupMenuTextJustification,
                                      std::move(onSelect));
                return;
            }

            if (auto* owner = findParentComponentOfClass<AvaAudioProcessorEditor>())
            {
                owner->showChoicePrompt(owner->getLocalArea(this, getLocalBounds()),
                                        itemTexts,
                                        currentSelectedItemIndex,
                                        std::move(itemEnabledStates),
                                        popupMenuTextJustification,
                                        std::move(onSelect));
                return;
            }
        }
    }

    auto menu = *getRootMenu();

    if (menu.getNumItems() == 0)
        menu.addItem(1, getTextWhenNoChoicesAvailable(), false, false);

    auto* label = dynamic_cast<juce::Label*>(getChildComponent(0));

    if (label == nullptr)
    {
        juce::ComboBox::showPopup();
        return;
    }

    auto& lf = getLookAndFeel();
    menu.setLookAndFeel(&lf);
    const auto currentSelectedId = getSelectedId();
    const auto shouldMarkSelectedItem = getNumItems() > 2;

    for (juce::PopupMenu::MenuItemIterator iterator(menu, true); iterator.next();)
    {
        auto& item = iterator.getItem();

        if (item.itemID != 0)
            item.isTicked = shouldMarkSelectedItem && item.itemID == currentSelectedId;
    }

    menu.showMenuAsync(lf.getOptionsForComboBoxPopupMenu(*this, *label),
                       juce::ModalCallbackFunction::create(
                           [safePointer = juce::Component::SafePointer<NoTickComboBox>(this), currentSelectedId] (int result)
                           {
                               if (safePointer == nullptr)
                                   return;

                               safePointer->hidePopup();

                               if (result != 0)
                               {
                                   const auto shouldReSelectCurrentItem = result == currentSelectedId;
                                   safePointer->setSelectedId(result);

                                   if (shouldReSelectCurrentItem && safePointer->onReselectedCurrentItem != nullptr)
                                       safePointer->onReselectedCurrentItem();
                               }

                               if (auto* handler = safePointer->getAccessibilityHandler())
                                   handler->grabFocus();
                           }));
}

void NoTickComboBox::mouseDown(const juce::MouseEvent& event)
{
    if (! isEnabled() || (! event.mods.isLeftButtonDown() && ! event.mods.isPopupMenu()))
        return;

    pointerDown = true;
    dragDetected = false;
    pressHighlight = true;
    repaint();

    if (event.mods.isPopupMenu())
    {
        showPopup();
        pointerDown = false;
        pressHighlight = false;
        return;
    }
}

void NoTickComboBox::mouseDrag(const juce::MouseEvent& event)
{
    if (pointerDown && ! dragDetected && event.mouseWasDraggedSinceMouseDown())
    {
        dragDetected = true;

        if (pressHighlight)
        {
            pressHighlight = false;
            repaint();
        }
    }
}

void NoTickComboBox::mouseUp(const juce::MouseEvent& event)
{
    if (! isEnabled())
        return;

    const auto shouldOpenPopup = pointerDown
        && ! dragDetected
        && ! event.mouseWasDraggedSinceMouseDown()
        && ! event.mods.isPopupMenu()
        && contains(event.getPosition());

    pointerDown = false;
    dragDetected = false;
    pressHighlight = false;
    repaint();

    if (shouldOpenPopup)
        showPopup();
}

void NoTickComboBox::mouseExit(const juce::MouseEvent&)
{
    if (pointerDown)
        pressHighlight = false;

    repaint();
}

void NoTickComboBox::mouseEnter(const juce::MouseEvent&)
{
    repaint();
}

void NoTickComboBox::resized()
{
    juce::ComboBox::resized();

    for (int childIndex = 0; childIndex < getNumChildComponents(); ++childIndex)
        if (auto* child = getChildComponent(childIndex))
            child->setMouseCursor(juce::MouseCursor::NormalCursor);
}

void NoTickComboBox::setPopupMenuTextJustification(const juce::Justification justification) noexcept
{
    popupMenuTextJustification = justification;
}

juce::Justification NoTickComboBox::getPopupMenuTextJustification() const noexcept
{
    return popupMenuTextJustification;
}

void NoTickComboBox::setPromptStylePopupEnabled(const bool shouldEnable) noexcept
{
    promptStylePopupEnabled = shouldEnable;
}

void NoTickComboBox::setChoicePromptPresenter(ChoicePromptPresenter presenter)
{
    choicePromptPresenter = std::move(presenter);
}

bool NoTickComboBox::isPressedHighlightEnabled() const noexcept
{
    return isEnabled() && ((pressHighlight && ! dragDetected) || isMouseHovering(*this));
}
