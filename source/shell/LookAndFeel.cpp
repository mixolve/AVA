#include "Editor.h"
#include "LookAndFeel.h"

namespace
{
constexpr int popupItemHeight = 30;

class ScrollingComboBoxLabel final : public juce::Label
{
public:
    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(findColour(juce::Label::textColourId));
        graphics.setFont(getFont());

        if (drawLoopingText(graphics,
                            getText(),
                            getLocalBounds().reduced(uiGap, 0),
                            getFont(),
                            getJustificationType()))
        {
            scheduleMarqueeRepaint(*this, marqueeRepaintPending);
        }
    }

private:
    bool marqueeRepaintPending = false;
};
}

AvaAudioProcessorEditor::AvaLookAndFeel::AvaLookAndFeel()
{
    if (auto typeface = getUiTypeface())
        setDefaultSansSerifTypeface(typeface);
}

juce::Typeface::Ptr AvaAudioProcessorEditor::AvaLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    if (auto typeface = getUiTypeface())
        return typeface;
    return LookAndFeel_V4::getTypefaceForFont(font);
}

juce::Font AvaAudioProcessorEditor::AvaLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return makeUiFont();
}

juce::Label* AvaAudioProcessorEditor::AvaLookAndFeel::createComboBoxTextBox(juce::ComboBox&)
{
    auto* label = new ScrollingComboBoxLabel();
    label->setEditable(false, false, false);
    label->setInterceptsMouseClicks(false, false);
    return label;
}

juce::Font AvaAudioProcessorEditor::AvaLookAndFeel::getPopupMenuFont()
{
    return makeUiFont();
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& graphics,
                                                                                 const int width,
                                                                                 const int height,
                                                                                 const juce::PopupMenu::Options&)
{
    graphics.setColour(uiBackground);
    graphics.fillRect(0, 0, width, height);
    graphics.setColour(uiWhite);
    graphics.drawRect(0, 0, width, height, 1);
}

int AvaAudioProcessorEditor::AvaLookAndFeel::getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&)
{
    return 1;
}

void AvaAudioProcessorEditor::AvaLookAndFeel::getIdealPopupMenuItemSizeWithOptions(const juce::String& text,
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

    idealWidth = juce::jmax(80, getTextPixelWidth(makeUiFont(), text) + uiGapDouble);
    idealHeight = popupItemHeight;
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& graphics, const juce::Path& path, juce::Image&)
{
    graphics.setColour(uiPopup);
    graphics.fillPath(path);
    graphics.setColour(uiGreyLight);
    graphics.strokePath(path, juce::PathStrokeType(1.0f));
}

int AvaAudioProcessorEditor::AvaLookAndFeel::getCallOutBoxBorderSize(const juce::CallOutBox&)
{
    return 1;
}

float AvaAudioProcessorEditor::AvaLookAndFeel::getCallOutBoxCornerSize(const juce::CallOutBox&)
{
    return 0.0f;
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawComboBox(juce::Graphics& g,
                                                           int width,
                                                           int height,
                                                           bool,
                                                           int,
                                                           int,
                                                           int,
                                                           int,
                                                           juce::ComboBox& box)
{
    auto* noTickBox = dynamic_cast<NoTickComboBox*>(&box);
    const auto hoverAmount = noTickBox != nullptr ? noTickBox->getHoverAmount() : (isMouseHovering(box)?1.0f:0.0f);
    ControlAppearance style(false,hoverAmount);
    style.normalFill=box.findColour(juce::ComboBox::backgroundColourId);
    style.normalContent=box.findColour(juce::ComboBox::textColourId);
    style.normalBorder=box.findColour(juce::ComboBox::outlineColourId);
    style.paint(g,juce::Rectangle<int>(0,0,width,height));
    if(auto* label=dynamic_cast<juce::Label*>(box.getChildComponent(0)))
        label->setColour(juce::Label::textColourId,style.contentColour());

}

void AvaAudioProcessorEditor::AvaLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    if (dynamic_cast<NoTickComboBox*>(&box) == nullptr)
    {
        juce::LookAndFeel_V4::positionComboBoxText(box, label);
        label.setFont(getComboBoxFont(box));
        return;
    }

    label.setBounds(1, 1,
                    box.getWidth() - 2,
                    box.getHeight() - 2);

    label.setFont(getComboBoxFont(box));
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                                                const juce::Rectangle<int>& area,
                                                                bool isSeparator,
                                                                bool isActive,
                                                                bool isHighlighted,
                                                                bool isTicked,
                                                                bool hasSubMenu,
                                                                const juce::String& text,
                                                                const juce::String& shortcutKeyText,
                                                                const juce::Drawable* icon,
                                                                const juce::Colour* textColour)
{
    juce::ignoreUnused(hasSubMenu, shortcutKeyText, icon, textColour);

    if (isSeparator)
    {
        g.setColour(uiGreyLight);
        g.fillRect(area.withHeight(1).withCentre(area.getCentre()));
        return;
    }

    const auto itemBounds = area.withSizeKeepingCentre(area.getWidth(),
                                                       juce::jmin(popupItemHeight, area.getHeight()));
    ControlAppearance style(isActive && isTicked,isHighlighted && isActive?1.0f:0.0f);
    style.paint(g,itemBounds);
    g.setColour(isActive?style.contentColour():uiGreyLight);
    g.setFont(makeUiFont());
    g.drawFittedText(text,
                     itemBounds.reduced(uiGap, 0),
                     juce::Justification::centred,
                     1,
                     1.0f);
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawPopupMenuItemWithOptions(juce::Graphics& g,
                                                                           const juce::Rectangle<int>& area,
                                                                           bool isHighlighted,
                                                                           const juce::PopupMenu::Item& item,
                                                                           const juce::PopupMenu::Options& options)
{
    if (item.isSeparator)
    {
        g.setColour(uiGreyLight);
        g.fillRect(area.withHeight(1).withCentre(area.getCentre()));
        return;
    }

    const auto itemBounds = area.withSizeKeepingCentre(area.getWidth(),
                                                       juce::jmin(popupItemHeight, area.getHeight()));
    ControlAppearance style(item.isEnabled && item.isTicked,isHighlighted && item.isEnabled?1.0f:0.0f);
    style.paint(g,itemBounds);
    g.setColour(item.isEnabled?style.contentColour():uiGreyLight);
    g.setFont(makeUiFont());
    const auto isNoTickTarget = dynamic_cast<NoTickComboBox*>(options.getTargetComponent()) != nullptr;
    const auto justification = isNoTickTarget ? dynamic_cast<NoTickComboBox*>(options.getTargetComponent())->getPopupMenuTextJustification()
                                              : juce::Justification::centred;
    g.drawFittedText(item.text,
                     itemBounds.reduced(uiGap, 0),
                     justification,
                     1,
                     1.0f);
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawTextEditorOutline(juce::Graphics& g,int width,int height,juce::TextEditor& editor)
{
    if(!editor.isEnabled()) return;
    ControlAppearance style(false,0.0f,editor.hasKeyboardFocus(true) && !editor.isReadOnly());
    style.normalBorder=editor.findColour(juce::TextEditor::outlineColourId);
    style.paintFrame(g,juce::Rectangle<int>(0,0,width,height));
}
