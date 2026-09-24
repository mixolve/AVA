#include "Controls.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"

#include <utility>

namespace
{
constexpr int popupItemHeight = 30;

void drawNeutralPopupItem(juce::Graphics& g,
                          const juce::Rectangle<int>& area,
                          const bool isSeparator,
                          const bool isEnabled,
                          const bool isHighlighted,
                          const juce::String& text,
                          const juce::String& shortcutKeyText,
                          const juce::Drawable* icon,
                          const juce::Colour* textColour,
                          const juce::Justification justification)
{
    juce::ignoreUnused(shortcutKeyText, icon);

    if (isSeparator)
    {
        g.setColour(uiGrey500);
        g.fillRect(area.withHeight(1).withCentre(area.getCentre()));
        return;
    }

    juce::ignoreUnused(isHighlighted);
    const auto itemBounds = area.withSizeKeepingCentre(area.getWidth(),
                                                       juce::jmin(popupItemHeight, area.getHeight()));
    g.setColour(uiGreyDark);
    g.fillRect(itemBounds);

    g.setColour(textColour != nullptr ? *textColour
                                      : (isEnabled ? uiWhite : uiGrey500));
    g.setFont(makeUiFont());
    g.drawFittedText(text,
                     itemBounds.reduced(uiGap, 0),
                     justification,
                     1,
                     1.0f);
}
}

CopyPasteTextEditor::CopyPasteTextEditor()
{
    setLookAndFeel(&popupLookAndFeel);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    setColour(juce::CaretComponent::caretColourId, uiWhite);
}

CopyPasteTextEditor::~CopyPasteTextEditor()
{
    setLookAndFeel(nullptr);
}

juce::Font CopyPasteTextEditor::PopupLookAndFeel::getPopupMenuFont()
{
    return makeUiFont();
}

void CopyPasteTextEditor::PopupLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& graphics,
                                                                               const int width,
                                                                               const int height,
                                                                               const juce::PopupMenu::Options&)
{
    graphics.setColour(uiGreyLight);
    graphics.fillRect(0, 0, width, height);
    graphics.setColour(uiWhite);
    graphics.drawRect(0, 0, width, height, 2);
}

int CopyPasteTextEditor::PopupLookAndFeel::getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&)
{
    return 2;
}

void CopyPasteTextEditor::PopupLookAndFeel::getIdealPopupMenuItemSizeWithOptions(const juce::String& text,
                                                                                 bool isSeparator,
                                                                                 int,
                                                                                 int& idealWidth,
                                                                                 int& idealHeight,
                                                                                 const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 0;
        idealHeight = 2;
        return;
    }

    idealWidth = juce::jmax(72, getTextPixelWidth(makeUiFont(), text) + uiGapDouble);
    idealHeight = popupItemHeight + uiGap;
}

void CopyPasteTextEditor::PopupLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                                             const juce::Rectangle<int>& area,
                                                             bool isSeparator,
                                                             bool isActive,
                                                             bool isHighlighted,
                                                             bool,
                                                             bool,
                                                             const juce::String& text,
                                                             const juce::String& shortcutKeyText,
                                                             const juce::Drawable* icon,
                                                             const juce::Colour* textColour)
{
    drawNeutralPopupItem(g,
                         area,
                         isSeparator,
                         isActive,
                         isHighlighted,
                         text,
                         shortcutKeyText,
                         icon,
                         textColour,
                         juce::Justification::centredLeft);
}

void CopyPasteTextEditor::PopupLookAndFeel::drawPopupMenuItemWithOptions(juce::Graphics& g,
                                                                         const juce::Rectangle<int>& area,
                                                                         bool isHighlighted,
                                                                         const juce::PopupMenu::Item& item,
                                                                         const juce::PopupMenu::Options&)
{
    drawNeutralPopupItem(g,
                         area,
                         item.isSeparator,
                         item.isEnabled,
                         isHighlighted,
                         item.text,
                         item.shortcutKeyDescription,
                         item.image.get(),
                         item.colour.isTransparent() ? nullptr : &item.colour,
                         juce::Justification::centredLeft);
}

void CopyPasteTextEditor::addPopupMenuItems(juce::PopupMenu& menuToAddTo, const juce::MouseEvent*)
{
    const auto hasSelection = ! getHighlightedRegion().isEmpty();
    const auto canPaste = ! isReadOnly();

    menuToAddTo.addItem(juce::StandardApplicationCommandIDs::copy, "Copy", hasSelection, false);
    menuToAddTo.addItem(juce::StandardApplicationCommandIDs::paste, "Paste", canPaste, false);
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
        if (auto* owner = findParentComponentOfClass<AvaAudioProcessorEditor>())
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

                const auto anchorBounds = owner->getLocalArea(this, getLocalBounds());
                const auto currentSelectedItemIndex = getSelectedItemIndex();

                owner->showChoicePrompt(anchorBounds,
                                        itemTexts,
                                        currentSelectedItemIndex,
                                        std::move(itemEnabledStates),
                                        popupMenuTextJustification,
                                        [safePointer = juce::Component::SafePointer<NoTickComboBox>(this), currentSelectedItemIndex] (int selectedIndex)
                                        {
                                            if (safePointer == nullptr)
                                                return;

                                            const auto shouldReSelectCurrentItem = selectedIndex == currentSelectedItemIndex;
                                            safePointer->setSelectedItemIndex(selectedIndex, juce::sendNotificationSync);

                                            if (shouldReSelectCurrentItem && safePointer->onReselectedCurrentItem != nullptr)
                                                safePointer->onReselectedCurrentItem();

                                            if (auto* handler = safePointer->getAccessibilityHandler())
                                                handler->grabFocus();
                                        });
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
    if (! isEnabled())
        return;

    shell_parameter_focus::clearFocus(*this);

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

void NoTickComboBox::setChoiceEnabled(const int choiceIndex, const bool shouldEnable)
{
    if (choiceIndex < 0)
        return;

    const auto itemIndex = choiceIndex + 1;

    if (itemIndex <= 0 || itemIndex > getNumItems())
        return;

    setItemEnabled(itemIndex, shouldEnable);
}

bool NoTickComboBox::isPressedHighlightEnabled() const noexcept
{
    return isEnabled() && ((pressHighlight && ! dragDetected) || isMouseHovering(*this));
}
