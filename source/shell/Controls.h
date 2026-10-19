#pragma once

#include "Style.h"
#include "AnimationSettings.h"
#include "HoverAnimation.h"
#include "NumericTextRaster.h"

#include <functional>
#include <memory>
#include <vector>

bool scrollViewportWithWheel(juce::Viewport& viewport,
                             int contentHeight,
                             const juce::MouseWheelDetails& wheel,
                             bool fineControl = false);

class FlatScrollBarLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                       bool vertical, int thumbStart, int thumbSize, bool hovered, bool pressed) override;
};

class BurningFilmArtwork final : public juce::Component, private juce::Timer
{
public:
    BurningFilmArtwork();
    ~BurningFilmArtwork() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void setGpuRenderingEnabled(bool enabled);
    void setPreviewEnabled(bool enabled);
    void setAnimationSettings(std::shared_ptr<AnimationSettings> settings);
    void setBottomAnimation(bool bottom);
    void setAnimationSource(BurningFilmArtwork& source);
    void setSynchronizationSource(BurningFilmArtwork& source);
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    std::function<void()> onSettingsRequested;
    std::function<void(float)> onCropChanged;

private:
    void timerCallback() override;
    void ensurePreview();
    void resetNameScroll();
    juce::Rectangle<float> getCropBounds() const;
    struct Animation;
    std::unique_ptr<Animation> animation;
    juce::Component::SafePointer<BurningFilmArtwork> animationSource;
    juce::Component::SafePointer<BurningFilmArtwork> synchronizationSource;
    bool cropDragActive = false;
    float cropDragStartY = 0, cropDragStartPosition = 0;
};

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
    bool isPressedHighlightEnabled() const noexcept;
    float getHoverAmount() { return hoverAnimation.amount(isPressedHighlightEnabled()); }

    std::function<void()> onReselectedCurrentItem;

private:
    juce::Justification popupMenuTextJustification = juce::Justification::centred;
    bool pointerDown = false;
    bool dragDetected = false;
    bool pressHighlight = false;
    bool promptStylePopupEnabled = false;
    ChoicePromptPresenter choicePromptPresenter;
    HoverAnimation hoverAnimation { *this };
};

class CopyPasteTextEditor final : public juce::TextEditor
{
public:
    CopyPasteTextEditor();
    void setText(const juce::String& text, bool sendChangeMessage = true)
    { juce::TextEditor::setText(uppercaseText(text), sendChangeMessage); }
    ~CopyPasteTextEditor() override;
    void mouseDown(const juce::MouseEvent& event) override;

    void addPopupMenuItems(juce::PopupMenu& menuToAddTo, const juce::MouseEvent* mouseClickEvent) override;
    void performPopupMenuAction(int menuItemID) override;

private:
    std::unique_ptr<juce::Component> contextMenu;

};

class InlineParameterSlider;

class ValueBoxComponent final : public juce::Component
{
public:
    explicit ValueBoxComponent(juce::Slider& sliderToControl);
    ~ValueBoxComponent() override;

    static bool refocusActiveInlineEditorIn(juce::Component& scope);
    static void cancelActiveInlineEditorIn(juce::Component& scope);

    void setInteractionEnabled(bool shouldEnable);
    void setOutlineColour(juce::Colour colour);
    void setPromptActive(bool shouldBeActive);
    void setCustomPromptAction(std::function<void()> action);
    void setNumericControlEnabled(bool enabled);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    std::function<juce::String()> displayTextProvider;
    std::function<juce::String()> editorTextProvider;
    std::function<double(const juce::String&)> textToValueParser;

private:
    void updateSliderAvailability();
    void showEditor();
    void applyValue(double value);
    void applyEnteredText(const juce::String& enteredText);
    void hideEditor(bool discard);
    void startGlobalEditTracking();
    void stopGlobalEditTracking();

    NumericTextRaster textRaster;
    HoverAnimation hoverAnimation { *this };
    juce::Slider& slider;
    bool isTrackingGlobalClicks = false;
    bool interactionEnabled = true;
    bool pointerDown = false;
    bool pressHighlight = false;
    bool promptActive = false;
    juce::Colour outlineColour = uiGreyLight;
    std::function<void()> customPromptAction;
    std::unique_ptr<juce::TextEditor> editor;
    bool marqueeRepaintPending = false;
    bool numericControlEnabled = true;
    std::unique_ptr<InlineParameterSlider> inlineSlider;
};

class BoxTextButton final : public juce::TextButton, private juce::Timer
{
public:
    explicit BoxTextButton(juce::Colour accent);
    ~BoxTextButton() override;

    void setActiveFill(bool shouldBeActive);
    bool hasActiveHighlight() const noexcept;
    float getActiveFillAmount() const noexcept { return activeFillAmount; }
    void paintOutline(juce::Graphics&, juce::Rectangle<int> bounds) const;
    void setToggleActiveFillEnabled(bool shouldShow) noexcept;
    void setInteractionOutlineEnabled(bool shouldEnable) noexcept;
    void setFillVisible(bool shouldShow) noexcept;
    void setFillColour(juce::Colour colour) noexcept;
    void setDividerLineVisible(bool shouldShow) noexcept;
    void setTextJustification(juce::Justification justification) noexcept;
    void setTextScrollingEnabled(bool enabled) { textScrollingEnabled = enabled; repaint(); }
    void setTextHorizontalPadding(int padding) { textHorizontalPadding = juce::jmax(0, padding); repaint(); }
    void setBorderVisible(bool shouldShow) noexcept;
    void setCancelClickOnLeave(bool shouldEnable) noexcept;
    void setTablerIcon(const char* iconName);
    bool usesIconOnlyContent() const noexcept;
    void setTextColourOverride(juce::Colour colour);
    void setLongPressPromptActions(std::function<void()> resetAction,
                                   std::function<void()> secondaryAction = {});
    void setLongPressPrimaryPromptIcon(const char* iconName);
    void setLongPressMovePromptEnabled(bool enabled) { longPressMovePromptEnabled = enabled; repaint(); }
    void setLongPressPrimaryPromptEnabled(bool shouldEnable);
    void setLongPressSecondaryPromptIcon(const char* iconName);
    void setLongPressSecondaryPromptActive(bool shouldBeActive);
    void setLongPressTrailingPromptIconAction(std::function<void()> action, const char* iconName);
    void flashConfirmationFill();
    void setDragTargetHighlighted(bool shouldShow) noexcept;
    void inheritHover(bool highlighted) { hoverAnimation.inherit(highlighted); }
    void setMoveTargetAvailable(bool available);
    juce::MouseCursor getMouseCursor() override;

    std::function<void()> onMoveArmed;
    std::function<void()> onDoubleClick;
    std::function<void()> onPopupClick;
    std::function<void(juce::ModifierKeys)> onModifiedClick;
    std::function<void()> onControlDragStarted;
    std::function<void(const juce::MouseEvent&, bool released)> onControlDrag;

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

    HoverAnimation hoverAnimation { *this };
    juce::Colour accentColour;
    bool activeFillEnabled = false;
    float activeFillAmount = 0.0f;
    float hoverOutlineAmount = 0.0f;
    bool toggleActiveFillEnabled = true;
    bool interactionOutlineEnabled = true;
    bool longPressMovePromptEnabled = true;
    bool fillVisible = true;
    juce::Colour fillColour = uiGreyDark;
    bool dividerLineVisible = false;
    bool pointerDown = false;
    bool dragActive = false;
    bool pressHighlight = false;
    bool cancelClickOnLeave = false;
    bool pressCanceled = false;
    juce::Justification textJustification = juce::Justification::centred;
    int textHorizontalPadding = uiGap;
    bool textScrollingEnabled = true;
    bool borderVisible = true;
    juce::Image tablerIconImage;
    bool hasTextColourOverride = false;
    juce::Colour textColourOverride;
    bool confirmationFlashActive = false;
    bool dragTargetHighlighted = false;
    bool moveTargetAvailable = false;
    int longPressDelayMs = 500;
    bool longPressEligible = false;
    std::function<void()> longPressResetAction;
    std::function<void()> longPressSecondaryAction;
    juce::Image longPressPrimaryPromptIconImage;
    bool longPressPrimaryPromptEnabled = true;
    juce::Image longPressSecondaryPromptIconImage;
    bool longPressSecondaryPromptActive = false;
    std::function<void()> longPressTrailingAction;
    juce::Image longPressTrailingPromptIconImage;
    std::unique_ptr<PromptDismissListener> promptDismissListener;
    bool actionPromptActive = false;
    bool actionPromptGlobalListenerActive = false;
    bool consumeNextMouseUp = false;
    int actionPromptPressedIndex = -1;
    int actionPromptHoverIndex = -1;
    juce::String actionPromptOriginalText;
    bool marqueeRepaintPending = false;
    bool controlDragActive = false;
    juce::ModifierKeys clickModifiers;
    int pendingClickGeneration = 0;

    void showActionPrompt();
    void dismissActionPrompt();
    int getActionPromptCount() const noexcept;
    juce::Rectangle<int> getActionPromptBounds(int index) const noexcept;
    int getActionPromptHitIndex(juce::Point<int> position) const noexcept;
    void timerCallback() override;
};
