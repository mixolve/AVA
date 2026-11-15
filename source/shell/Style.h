#pragma once

#include "Metrics.h"
#include "../modules/shared/ParameterFormatting.h"

#include <JuceHeader.h>
#include <vector>

void paintPluginBackground(juce::Graphics& graphics);
juce::Rectangle<int> getControlBlockFrameBounds(juce::Rectangle<int> content) noexcept;
juce::Rectangle<int> getModuleBlockFrameBounds(juce::Rectangle<int> blocks) noexcept;
void paintControlBlockFrame(juce::Graphics&, juce::Rectangle<int>, const juce::String& title,
                            juce::Colour colour = juce::Colour(0xffffffff), bool hovered = false,
                            bool collapsed = false, bool centreInkVertically = false);
int getEditorContentInset(const juce::Component& component) noexcept;
int getEditorHeight(const juce::Component& component) noexcept;
int getEditorWidth(const juce::Component& component) noexcept;
int getExpandedEditorWidth() noexcept;
int getDockLength(bool vertical, int controls = viewportControlColumns) noexcept;

inline constexpr int defaultUiCornerRadius = 0;
juce::Path makeUiCornerPath(juce::Rectangle<float> bounds, float radius = defaultUiCornerRadius);
void paintUiFrame(juce::Graphics& graphics, juce::Rectangle<float> bounds,
                  juce::Colour colour, float thickness, float radius = defaultUiCornerRadius);

bool isManualControlSelected(const juce::Component& component);


void paintIndicator(juce::Graphics& graphics, juce::Rectangle<float> bounds, juce::Colour colour, float radius = defaultUiCornerRadius);

std::vector<juce::Component*> getForegroundOccluders(juce::Component& root);
juce::Rectangle<int> getVisibleControlBounds(const juce::Component& component, const juce::Component& root);

int getUiGap(const juce::Component& component) noexcept;
int getUiRowGap(const juce::Component& component) noexcept;
int getSquareControlSize(const juce::Component& component) noexcept;
int getCompactControlSize(const juce::Component& component) noexcept;

struct ControlGridCell { juce::Component* component; int span = 1; bool startNewRow = false; };
juce::Rectangle<int> measureControlGrid(const juce::Component&, juce::Rectangle<int>,
                                      const std::vector<ControlGridCell>&);
juce::Rectangle<int> layoutControlGrid(const juce::Component& owner, juce::Rectangle<int> bounds,
                                        const std::vector<ControlGridCell>& cells);

inline constexpr int squareControlSize = 80;
inline constexpr int standardControlTextZoneHeight = 44; // 1px border + 42px interior + 1px border
inline constexpr int maximumStoredEditorDimension = 4096;
inline constexpr int rowHeight = 30;
inline constexpr float uiFontSize = 22.0f;

inline const auto uiPanelBackground = juce::Colour(0xff666666);
inline const auto uiWhite = juce::Colour(0xffffffff);
inline const auto uiIndicatorActive = juce::Colour(0xffbbbbbb);
// Idle indicators use the same light gray as other idle controls.
// Disabled/unavailable indicators are darker; pressed/selected actions are white.
inline const auto uiIndicatorInactive = juce::Colour(0xffbbbbbb);
inline const auto uiIndicatorDisabled = juce::Colour(0xff888888);
inline const auto uiGreyLight = juce::Colour(0xffbbbbbb);
inline const auto uiGreyDark = juce::Colour(0xff444444);
// Construct each colour independently: cross-TU dynamic initialisation can
// otherwise copy a not-yet-initialised inline colour in Windows builds.

#include "ControlAppearance.h"

juce::Typeface::Ptr getUiTypeface();
juce::FontOptions makeUiFontOptions();
juce::Font makeUiFont();
juce::Rectangle<int> getControlTextBounds(const juce::Component& component, juce::Rectangle<int> bounds) noexcept;
bool drawCompactControlText(juce::Graphics& graphics, const juce::String& text,
                            juce::Rectangle<int> bounds,
                            juce::Justification justification = juce::Justification::centred);
juce::String visibleControlCaption(const juce::String& text);
bool drawSquareControlText(juce::Graphics& graphics, const juce::String& text,
                           juce::Rectangle<int> bounds,
                           juce::Justification justification = juce::Justification::centred);
// Read-only status tiles may need more than the two lines used by parameter controls.
// Fit every line uniformly so the last line remains visible at compact zoom levels.
void drawMultilineStatusText(juce::Graphics& graphics, const juce::String& text,
                             juce::Rectangle<int> bounds);
int getTextPixelWidth(const juce::Font& font, const juce::String& text);
bool drawLoopingText(juce::Graphics& graphics,
                     const juce::String& text,
                     const juce::Rectangle<int>& bounds,
                     const juce::Font& font,
                     juce::Justification justification = juce::Justification::centred,
                     double elapsedSeconds = -1.0,
                     float verticalOffset = 0.0f);
// pending must belong to component, so both share the same lifetime.
void scheduleMarqueeRepaint(juce::Component& component, bool& pending);
using ava::modules::parameters::formatFixedDecimalValue;
using ava::modules::parameters::formatRangeDecimalValue;
using ava::modules::parameters::parseNumericInput;
double findNearestChoiceIndex(double targetValue, const juce::StringArray& choices, const juce::String& enteredText);
void clearKeyboardFocus(juce::Component& component);
bool isMouseHovering(const juce::Component& component) noexcept;
bool isMouseHovering(const juce::Component& component,juce::Rectangle<int> region) noexcept;
// Visual inversion appears 50 ms after pointer entry; never delays click handling.
bool isHoverInversionReady(juce::Component& component, bool hovering, const char* region = "main");
inline bool isCtrlClick(const juce::MouseEvent& event) noexcept
{
    return event.mods.isCtrlDown() && event.mods.isLeftButtonDown();
}
bool isEmptyEditorBackground(const juce::Component* component);

inline juce::Rectangle<int> controlStripCell(juce::Rectangle<int> strip, int slot, int columns)
{
    columns = juce::jmax(1, columns);
    slot = juce::jlimit(0, columns - 1, slot);
    const auto usable = juce::jmax(0, strip.getWidth() - controlInnerGap * (columns - 1));
    const auto left = strip.getX() + usable * slot / columns + controlInnerGap * slot;
    const auto right = strip.getX() + usable * (slot + 1) / columns + controlInnerGap * slot;
    return {left, strip.getY(), right - left, strip.getHeight()};
}

bool drawEmbeddedButtonText(juce::Graphics&,const juce::String&,juce::Rectangle<int>, juce::Colour colour = juce::Colour(0xff222222));

inline void paintControlTextButton(juce::Graphics& g, juce::Rectangle<int> bounds, bool hovered = false, bool active = false, bool pressed = false, juce::Colour normal = juce::Colour(0xffbbbbbb))
{
    const auto base = active || pressed ? juce::Colour(0xffffffff) : normal;
    const auto inverted = hovered && !pressed;
    g.setColour(inverted ? juce::Colour(0xff666666) : base);
    g.fillRect(bounds);
    g.setColour(inverted ? juce::Colour(0xffffffff) : juce::Colour(0xff222222));
}
