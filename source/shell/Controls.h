#pragma once

#include "Style.h"

#include <functional>
#include <memory>
#include <vector>

bool scrollViewportWithWheel(juce::Viewport& viewport,
                             int contentHeight,
                             const juce::MouseWheelDetails& wheel,
                             bool fineControl = false);

class NoTickComboBox final : public juce::ComboBox
{
public:
    using ChoicePromptPresenter = std::function<void(const juce::StringArray&,
                                                     int,
                                                     std::vector<bool>,
                                                     juce::Justification,
                                                     std::function<void(int)>)>;

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
    void setChoicePromptPresenter(ChoicePromptPresenter presenter);
    void setChoiceEnabled(int choiceIndex, bool shouldEnable);
    bool isPressedHighlightEnabled() const noexcept;

    std::function<void()> onReselectedCurrentItem;

private:
    juce::Justification popupMenuTextJustification = juce::Justification::centred;
    bool pointerDown = false;
    bool dragDetected = false;
    bool pressHighlight = false;
    bool promptStylePopupEnabled = false;
    ChoicePromptPresenter choicePromptPresenter;
};

class CopyPasteTextEditor final : public juce::TextEditor
{
public:
    CopyPasteTextEditor();
    ~CopyPasteTextEditor() override;

    void addPopupMenuItems(juce::PopupMenu& menuToAddTo, const juce::MouseEvent* mouseClickEvent) override;
    void performPopupMenuAction(int menuItemID) override;

private:
    class PopupLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
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

    PopupLookAndFeel popupLookAndFeel;
};

class ValueBoxComponent final : public juce::Component
{
public:
    explicit ValueBoxComponent(juce::Slider& sliderToControl);
    ~ValueBoxComponent() override;

    void setInteractionEnabled(bool shouldEnable);
    void setOutlineColour(juce::Colour colour);
    void setHighlightColour(juce::Colour colour);
    void setPromptActive(bool shouldBeActive);
    void setCustomPromptAction(std::function<void()> action);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    std::function<juce::String()> displayTextProvider;
    std::function<juce::String()> editorTextProvider;
    std::function<double(const juce::String&)> textToValueParser;
    std::function<void()> onBeforeShowEditor;

private:
    void updateMouseCursor();
    void showEditor();
    void applyValue(double value);
    void applyEnteredText(const juce::String& enteredText);
    void hideEditor(bool discard);
    void startGlobalEditTracking();
    void stopGlobalEditTracking();
    void scheduleMarqueeRepaint();

    juce::Slider& slider;
    bool isTrackingGlobalClicks = false;
    bool interactionEnabled = true;
    bool pointerDown = false;
    bool pressHighlight = false;
    bool promptActive = false;
    juce::Colour outlineColour = uiGrey500;
    juce::Colour highlightColour = uiBlack;
    std::function<void()> customPromptAction;
    std::unique_ptr<juce::TextEditor> editor;
    bool marqueeRepaintPending = false;
};

class BoxTextButton final : public juce::TextButton, private juce::Timer
{
public:
    explicit BoxTextButton(juce::Colour accent);
    ~BoxTextButton() override;

    void setAlwaysAccentOutline(bool shouldAlwaysAccent);
    void setToggleAccentVisible(bool shouldShow) noexcept;
    void setPressFillEnabled(bool shouldEnable) noexcept;
    void setClearsParameterFocusOnMouseDown(bool shouldClear) noexcept;
    void setFillVisible(bool shouldShow) noexcept;
    void setFillColour(juce::Colour colour) noexcept;
    void setInteractionFillColour(juce::Colour colour) noexcept;
    void setDividerLineVisible(bool shouldShow) noexcept;
    void setTextJustification(juce::Justification justification) noexcept;
    void setBorderVisible(bool shouldShow) noexcept;
    void setCancelClickOnLeave(bool shouldEnable) noexcept;
    void setHorizontalBidirectionalArrowVisible(bool shouldShow) noexcept;
    void setTablerIcon(const char* iconName);
    void setIconOnlyText(const juce::String& text);
    bool usesIconOnlyContent() const noexcept;
    void setTextColourOverride(juce::Colour colour);
    void clearTextColourOverride();
    void setLongPressAction(std::function<void()> action, int delayMs = 500, juce::String promptText = "RESET?");
    void setLongPressPromptActions(std::function<void()> resetAction,
                                   std::function<void()> hostAction = {},
                                   juce::String primaryPromptText = "R?");
    void setLongPressTrailingPromptAction(std::function<void()> action, juce::String promptText);
    void setLongPressTrailingPromptIconAction(std::function<void()> action, const char* iconName);
    void setLongPressAdditionalPromptIconAction(std::function<void()> action, const char* iconName);
    void setDragTargetOutlineVisible(bool shouldShow) noexcept;
    void flashConfirmationOutline();

    std::function<void(juce::Point<int>)> onDragDrop;
    std::function<void(juce::Point<int>)> onDragMove;
    std::function<void()> onDragEnd;
    std::function<void()> onMoveArmed;
    std::function<void()> onDoubleClick;

    void paintButton(juce::Graphics& graphics, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void enablementChanged() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    class PromptDismissListener;

    juce::Colour accentColour;
    bool alwaysAccentOutline = false;
    bool toggleAccentVisible = true;
    bool pressFillEnabled = true;
    bool clearsParameterFocusOnMouseDown = true;
    bool fillVisible = true;
    juce::Colour fillColour = uiGrey800;
    juce::Colour interactionFillColour = uiGreyLight;
    bool dividerLineVisible = false;
    bool pointerDown = false;
    bool dragActive = false;
    bool pressHighlight = false;
    bool cancelClickOnLeave = false;
    bool pressCanceled = false;
    juce::Justification textJustification = juce::Justification::centred;
    bool borderVisible = true;
    bool horizontalBidirectionalArrowVisible = false;
    juce::Image horizontalBidirectionalArrowImage;
    juce::Image tablerIconImage;
    bool iconOnlyText = false;
    bool hasTextColourOverride = false;
    juce::Colour textColourOverride;
    bool dragTargetOutlineVisible = false;
    bool confirmationFlashActive = false;
    std::function<void()> longPressAction;
    int longPressDelayMs = 500;
    bool longPressEligible = false;
    bool longPressArmed = false;
    juce::String longPressOriginalText;
    juce::String longPressPromptText = "RESET?";
    bool dragHoldEligible = false;
    bool dragHoldArmed = false;
    std::function<void()> longPressResetAction;
    std::function<void()> longPressHostAction;
    juce::String longPressPrimaryPromptText = "R?";
    std::function<void()> longPressTrailingAction;
    juce::String longPressTrailingPromptText;
    juce::Image longPressTrailingPromptIconImage;
    bool longPressTrailingPromptIsHostIcon = false;
    std::function<void()> longPressAdditionalPromptAction;
    juce::Image longPressAdditionalPromptIconImage;
    std::unique_ptr<PromptDismissListener> promptDismissListener;
    bool actionPromptActive = false;
    bool actionPromptGlobalListenerActive = false;
    bool consumeNextMouseUp = false;
    int actionPromptPressedIndex = -1;
    int actionPromptHoverIndex = -1;
    bool moveOnNextDrag = false;
    juce::String moveArmedOriginalText;
    juce::String actionPromptOriginalText;
    bool marqueeRepaintPending = false;
    int pendingClickGeneration = 0;

    void showActionPrompt();
    void dismissActionPrompt();
    int getActionPromptCount() const noexcept;
    juce::Rectangle<int> getActionPromptBounds(int index) const noexcept;
    int getActionPromptHitIndex(juce::Point<int> position) const noexcept;
    void scheduleMarqueeRepaint();
    void timerCallback() override;
};
