#pragma once

#include "Style.h"
#include "UppercaseText.h"
#include "DragCursor.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

void repaintValueOverlay(juce::Component& component);

class ControlSelectionGesture
{
public:
    bool begin(juce::Component& source, const juce::MouseEvent& event);
    bool finish(const juce::MouseEvent& event);
    bool isActive() const noexcept { return target != nullptr; }
private:
    juce::Component::SafePointer<juce::Component> target;
};

bool scrollViewportWithWheel(juce::Viewport& viewport,
                             int contentHeight,
                             const juce::MouseWheelDetails& wheel,
                             bool fineControl = false);

class UiScrollViewport : public juce::Viewport
{
public:
    std::function<void()> onViewChanged;
    std::function<void()> onBackgroundClick;
    void mouseDown(const juce::MouseEvent&) override
    {
        if (onBackgroundClick) onBackgroundClick();
    }
    void visibleAreaChanged(const juce::Rectangle<int>&) override
    {
        if (onViewChanged) onViewChanged();
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const auto height = getViewedComponent() != nullptr ? getViewedComponent()->getHeight() : 0;
        if (!scrollViewportWithWheel(*this, height, wheel, event.mods.isShiftDown()))
            juce::Component::mouseWheelMove(event, wheel);
    }
};

inline juce::Rectangle<int> moduleNameplateBypassBounds(juce::Rectangle<int> frame)
{
    const auto header = frame.withHeight(controlBlockHeaderHeight);
    constexpr int inset = controlEdgeInset;
    const auto side = header.getHeight() - 2 * inset;
    return {header.getRight() - inset - side,
            header.getY() + inset,
            side, side};
}

struct ControlBlockFrame
{
    juce::Rectangle<int> bounds;
    juce::String title;
    juce::Colour colour = uiWhite;
    juce::String displayTitle {};
    bool centreInkVertically = false;
};

class ControlBlockComponent : public juce::Component
{
public:
    void paintOverChildren(juce::Graphics& graphics) override
    {
        // Parent layouts provide the standard 8px inset for nested blocks.
        // Do not shrink only their painted frames: the header, controls and
        // mouse interaction must share the same bounds.
        for (const auto& frame : controlBlockFrames)
        {
            const bool headerHighlighted = (hoveredBlockVisualReady && hoveredBlockTitle == frame.title)
                || searchHighlightedTitle == frame.title;
            paintControlBlockFrame(graphics, frame.bounds,
                frame.displayTitle.isNotEmpty() ? frame.displayTitle : frame.title, frame.colour,
                headerHighlighted,
                bool(getProperties().getWithDefault("headerOnlyBlock", false)) || isBlockCollapsed(frame.title),
                frame.centreInkVertically);
            if (moduleHeaderToggle && frame.colour == uiGreyDark)
            {
                const auto bp = moduleNameplateBypassBounds(frame.bounds);
                const bool bypassed = moduleHeaderBypassed && moduleHeaderBypassed();
                graphics.setColour(hoveredBypassTitle == frame.title ? juce::Colour(0xffdddddd)
                    : bypassed ? uiWhite
                    : headerHighlighted ? uiGreyDark : uiGreyLight);
                graphics.fillRect(bp);
            }
        }
    }
    void setModuleNameplateBypass(std::function<bool()> isBypassed,
                                  std::function<void()> toggle)
    {
        moduleHeaderBypassed = std::move(isBypassed);
        moduleHeaderToggle = std::move(toggle);
        repaint();
    }
    // Collapse preferences are stored on the owning instance and scoped by BAND.
    void bindCollapseState(juce::AudioProcessorValueTreeState& state, juce::String scope);
    bool isBlockCollapsed(const juce::String& title) const;
    void collapseBlockInitially(const juce::String& title) { if (collapsible) collapsedBlockTitles.addIfNotAlreadyThere(title); }
    void setCollapsible(bool value) { collapsible = value; if (!value) collapsedBlockTitles.clear(); repaint(); }
    bool expandAllBlocks();
    bool collapseAllBlocks();
    void setSearchHighlightedTitle(const juce::String& title)
    {
        if (searchHighlightedTitle == title) return;
        searchHighlightedTitle = title;
        repaint();
    }
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    juce::MouseCursor getMouseCursor() override;
    std::vector<ControlBlockFrame> controlBlockFrames;
private:
    juce::String blockHeaderAt(juce::Point<int>) const;
    void updateHoveredBlock(juce::Point<int>);
    void setBlockCollapsed(const juce::String& title, bool collapsed);
    juce::Identifier collapseProperty(const juce::String& title) const;
    juce::AudioProcessorValueTreeState* collapseState = nullptr;
    juce::String collapseScope;
    // Defaults are kept separately: an explicit false overrides an initially
    // collapsed header (CROSSOVER/INPUT) after a restore.
    juce::StringArray collapsedBlockTitles;
    juce::String hoveredBlockTitle;
    juce::String hoveredBypassTitle;
    juce::String searchHighlightedTitle;
    bool hoveredBlockVisualReady = false;
    int hoverGeneration = 0;
    bool collapsible = true;
    std::function<bool()> moduleHeaderBypassed;
    std::function<void()> moduleHeaderToggle;
};

class CopyPasteTextEditor : public juce::TextEditor
{
public:
    CopyPasteTextEditor();
    bool keyPressed(const juce::KeyPress& key) override;
    void paint(juce::Graphics& g) override;
    void setText(const juce::String& text, bool sendChangeMessage = true)
    { juce::TextEditor::setText(uppercaseText(text), sendChangeMessage); }
    ~CopyPasteTextEditor() override;
    void mouseDown(const juce::MouseEvent& event) override;

    void addPopupMenuItems(juce::PopupMenu& menuToAddTo, const juce::MouseEvent* mouseClickEvent) override;
    void performPopupMenuAction(int menuItemID) override;

private:
    std::unique_ptr<juce::Component> contextMenu;

};

class SquareTextLabel final : public juce::Label
{
public:
    SquareTextLabel() { getProperties().set("passiveTextControl", true); }
    void mouseDown(const juce::MouseEvent& event) override { selectionGesture.begin(*this, event); }
    void mouseUp(const juce::MouseEvent& event) override { selectionGesture.finish(event); }
    void paint(juce::Graphics& g) override
    {
        const auto background = findColour(backgroundColourId);
        paintControlFill(g, getLocalBounds(), background.isTransparent() ? uiGreyDark : background);
        paintUiFrame(g, getLocalBounds().toFloat(), isManualControlSelected(*this) ? uiWhite : findColour(outlineColourId),
                     ControlAppearance::pixelThickness(g, 1.0f), float(defaultUiCornerRadius));
        const auto textBounds = getControlTextBounds(*this, getLocalBounds());
        paintControlTextButton(g, textBounds, false, false, false, isEnabled() ? uiGreyLight : uiIndicatorDisabled);
        const auto compact = getWidth() <= getCompactControlSize(*this) + 2
            && getHeight() <= getCompactControlSize(*this) + 2;
        if (bool(getProperties().getWithDefault("multilineStatusText", false)))
        {
            // Keep both lines centred in the same text zone used by the
            // ordinary square controls, not across the whole 80px tile.
            const auto zoneHeight = juce::jmin(textBounds.getHeight(),
                standardControlTextZoneHeight);
            drawMultilineStatusText(g, getText(),
                textBounds.withSizeKeepingCentre(textBounds.getWidth(), zoneHeight));
        }
        else
        {
            const auto scrolling = compact ? drawCompactControlText(g, getText(), textBounds, getJustificationType())
                : drawSquareControlText(g, getText(), textBounds, getJustificationType());
            if (scrolling) scheduleMarqueeRepaint(*this, marqueeRepaintPending);
        }
    }
private:
    ControlSelectionGesture selectionGesture;
    bool marqueeRepaintPending = false;
};

class RangeIndicator;
class BoxTextButton;

class ValueBoxComponent final : public juce::Component, public juce::SettableTooltipClient,
                                private juce::Slider::Listener, private juce::Timer
{
public:
    explicit ValueBoxComponent(juce::Slider& sliderToControl);
    ~ValueBoxComponent() override;

    static void paintRangeIndicators(juce::Graphics& g, juce::Component& root,
        const std::function<bool(const juce::Component&)>& include = {});
    struct BottomIndicator
    {
        std::function<int()> read;
        std::function<void(bool)> write;
        int state() const { return read ? read() : -1; }
        void paint(juce::Graphics& graphics, juce::Rectangle<int> bounds, bool enabled, float radius = defaultUiCornerRadius) const
        {
            const auto currentState = state();
            const auto colour = !enabled || currentState < 0 ? uiIndicatorDisabled
                              : currentState == 1 ? uiWhite : uiIndicatorInactive;
            paintIndicator(graphics, bounds.toFloat(), colour, radius);
        }
        void toggle() const
        {
            const auto value = state();
            const auto action = write;
            if (value >= 0 && action) action(value == 0);
        }
    };
    std::array<BottomIndicator, 2> bottomIndicators;
    bool isBottomIndicatorVisible(int index) const;
    bool areRangeIndicatorsVisible() const;
    juce::Rectangle<int> getBottomIndicatorBounds(juce::Rectangle<int> bounds, int index) const;

    void setInteractionEnabled(bool shouldEnable);
    void setMomentaryAction(std::function<void()> action) { momentaryAction = std::move(action); }
    void setMomentaryPressFeedbackEnabled(bool enabled) noexcept { momentaryPressFeedbackEnabled = enabled; }
    std::function<void(const juce::MouseEvent&, bool released)> onMomentaryDrag;
    std::function<void()> onMomentaryDragCancelled;
    void setDragTargetHighlighted(bool highlighted) { if (dragTargetHighlighted != highlighted) { dragTargetHighlighted = highlighted; repaintValueOverlay(*this); } }
    void setMomentaryRestingState(bool active);
    void setNumericControlEnabled(bool enabled);
    void setSelectionHighlighted(bool selected) { if (selectionHighlighted != selected) { selectionHighlighted = selected; repaint(); } }
    void setTextInputEnabled(bool enabled) { textInputEnabled = enabled; }
    void enablementChanged() override;
    void setDiscreteChoiceIndicator(int count, int selected = -1);
    std::function<void()> onResetRequested;
    void setIdleTextProvider(std::function<juce::String()> provider);
    juce::Rectangle<int> getValueBounds() const;
    bool hasTopInternalControls() const noexcept;
    bool hasBottomInternalControls() const noexcept;
    int bottomIndicatorAt(juce::Point<int>) const;
    double getDisplayedValue() const;
    bool hitTest(int x,int y) override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;

    std::function<juce::String()> displayTextProvider;
    std::function<bool(int)> isDiscreteChoiceEnabled;
    std::function<juce::String()> editorTextProvider;
    std::function<bool(const juce::String&)> textCommit;
    std::function<double(const juce::String&)> textToValueParser;

private:
    static constexpr double valueTransitionDelayMs = 50.0;
    ControlSelectionGesture selectionGesture;
    void updateDockInstanceHover(juce::Point<int> position);
    int pressedBottomIndicator = -1;
    bool canUseBottomIndicator(int index) const;
    bool canAdjustValue() const noexcept;
    void cancelMomentaryPress();
    void paintValueFrame(juce::Graphics&, juce::Rectangle<int>, bool overlayOnly = false);
    void finishIndicatorDrag();
    std::function<juce::String()> idleTextProvider;
    bool valueVisible = false;
    void timerCallback() override;
    void revealValue();
    bool pointerOnValue() const;
    int choiceCount() const noexcept { return discreteChoiceCount; }
    double valueTransitionAt = 0.0;
    void sliderValueChanged(juce::Slider*) override;
    void showEditor();
    void applyValue(double value);
    bool applyEnteredText(const juce::String& enteredText);

    std::unique_ptr<RangeIndicator> directIndicator;
    juce::Slider& slider;
    bool interactionEnabled = true;
    bool pointerDown = false;
    bool momentaryPressed = false;
    bool momentaryDragging = false, dragTargetHighlighted = false;
    juce::MouseCursor momentaryDragCursor;
    bool momentaryRestingState = false;
    bool momentaryPressFeedbackEnabled = true;
    bool selectionHighlighted = false;
    std::function<void()> momentaryAction;
    bool textInputActive = false;
    void setTextInputActive(bool active);
    bool marqueeRepaintPending = false;
    bool dockInstanceNameHovered = false;
    bool dockInstanceBadgeUnderPointer = false;
    unsigned dockInstanceHoverGeneration = 0;
    bool numericControlEnabled = true;
    bool textInputEnabled = true;
    int discreteChoiceCount = 0, discreteChoiceIndex = -1;
};

class BoxTextButton final : public juce::TextButton
{
public:
    BoxTextButton();
    void setSelectionHighlighted(bool shouldBeActive);
    bool hasActiveHighlight() const noexcept;
    void paintOutline(juce::Graphics&, juce::Rectangle<int> bounds) const;
    void setInteractionOutlineEnabled(bool shouldEnable) noexcept;
    void setFillColour(juce::Colour colour) noexcept;
    void setTextJustification(juce::Justification justification) noexcept;
    void setBorderVisible(bool shouldShow) noexcept;
    void setCancelClickOnLeave(bool shouldEnable) noexcept;
    void flashConfirmation();
    void setDragTargetHighlighted(bool shouldShow) noexcept;
    void setMoveTargetAvailable(bool available);
    void setDragRequiresControl(bool required) noexcept { dragRequiresControl = required; }
    void setBottomIndicator(int index, ValueBoxComponent::BottomIndicator indicator, juce::String label = {});
    juce::Rectangle<int> getBottomIndicatorBounds(int index) const;
    int bottomIndicatorAt(juce::Point<int> point) const;
    void refreshBottomIndicatorStates();
    juce::MouseCursor getMouseCursor() override;

    std::function<void()> onShiftClick;
    std::function<void(juce::ModifierKeys)> onModifiedClick;
    std::function<void()> onControlDragStarted;
    std::function<void(const juce::MouseEvent&, bool released)> onControlDrag;

    void paintButton(juce::Graphics& graphics, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void enablementChanged() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent&) override { repaint(); }
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    bool hasBottomInternalControls() const noexcept { return std::any_of(bottomIndicatorVisible.begin(), bottomIndicatorVisible.end(), [](bool visible) { return visible; }); }
private:
    bool selectionHighlighted = false;
    bool interactionOutlineEnabled = true;
    juce::Colour fillColour = uiGreyDark;
    bool pointerDown = false;
    bool dragActive = false;
    bool pressHighlight = false;
    bool cancelClickOnLeave = false;
    bool pressCanceled = false;
    juce::Justification textJustification = juce::Justification::centred;
    bool borderVisible = true;
    bool confirmationFlashActive = false;
    bool dragTargetHighlighted = false;
    bool moveTargetAvailable = false;
    bool marqueeRepaintPending = false;
    bool controlDragActive = false;
    bool dragRequiresControl = true;
    std::array<ValueBoxComponent::BottomIndicator, 3> bottomIndicators;
    std::array<int, 3> lastBottomIndicatorStates { -2, -2, -2 };
    std::array<juce::String, 3> bottomIndicatorLabels { "SL", "BP", "" };
    std::array<bool, 3> bottomIndicatorVisible {};
    ControlSelectionGesture selectionGesture;
    void paintBottomIndicators(juce::Graphics&);
    void updateDockBadgeTooltip(juce::Point<int> position);
    int pressedBottomIndicator = -1;
    juce::ModifierKeys clickModifiers;

};
