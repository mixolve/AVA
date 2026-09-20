#pragma once

#include "Controls.h"
#include "Editor.h"


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
