#pragma once

#include "Controls.h"
#include "Editor.h"

class AvaAudioProcessorEditor::AvaLookAndFeel final : public juce::LookAndFeel_V4, private juce::Timer
{
public:
    AvaLookAndFeel();
    bool areScrollbarButtonsVisible() override { return false; }
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int, int, int, int, bool, int, int, bool, bool) override;
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    juce::Font getPopupMenuFont() override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawPopupMenuBackgroundWithOptions(juce::Graphics&, int width, int height,
                                            const juce::PopupMenu::Options&) override;
    int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions(const juce::String&, bool isSeparator,
                                              int standardMenuItemHeight, int& idealWidth, int& idealHeight,
                                              const juce::PopupMenu::Options&) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour*) override;
    void drawPopupMenuItemWithOptions(juce::Graphics&, const juce::Rectangle<int>&, bool isHighlighted,
                                      const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
private:
    void trackScrollbarAnimation(juce::ScrollBar& bar);
    void timerCallback() override;
    std::vector<juce::Component::SafePointer<juce::ScrollBar>> animatedScrollbars;

};
