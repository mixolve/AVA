#pragma once

#include "UiControls.h"
#include "Editor.h"

#include <functional>
#include <memory>

class NoTickComboBox final : public juce::ComboBox
{
public:
    using juce::ComboBox::ComboBox;

    NoTickComboBox();

    void showPopup() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void resized() override;

    void setPopupMenuTextJustification(juce::Justification justification) noexcept;
    juce::Justification getPopupMenuTextJustification() const noexcept;
    void setPromptStylePopupEnabled(bool shouldEnable) noexcept;
    void setChoiceEnabled(int choiceIndex, bool shouldEnable);
    bool isPressedHighlightEnabled() const noexcept;

    std::function<void()> onReselectedCurrentItem;

private:
    juce::Justification popupMenuTextJustification = juce::Justification::centred;
    bool pointerDown = false;
    bool dragDetected = false;
    bool pressHighlight = false;
    bool promptStylePopupEnabled = false;
};

class AvaAudioProcessorEditor::AvaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AvaLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Label* createComboBoxTextBox(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
                                            int width,
                                            int height,
                                            const juce::PopupMenu::Options&) override;
    int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions(const juce::String& text,
                                              bool isSeparator,
                                              int standardMenuItemHeight,
                                              int& idealWidth,
                                              int& idealHeight,
                                              const juce::PopupMenu::Options&) override;
    void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override;
    int getCallOutBoxBorderSize(const juce::CallOutBox&) override;
    float getCallOutBoxCornerSize(const juce::CallOutBox&) override;
    void drawComboBox(juce::Graphics& g,
                      int width,
                      int height,
                      bool isButtonDown,
                      int buttonX,
                      int buttonY,
                      int buttonW,
                      int buttonH,
                      juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    void drawPopupMenuItem(juce::Graphics& g,
                           const juce::Rectangle<int>& area,
                           bool isSeparator,
                           bool isActive,
                           bool isHighlighted,
                           bool isTicked,
                           bool hasSubMenu,
                           const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void drawPopupMenuItemWithOptions(juce::Graphics& g,
                                      const juce::Rectangle<int>& area,
                                      bool isHighlighted,
                                      const juce::PopupMenu::Item& item,
                                      const juce::PopupMenu::Options& options) override;
};
