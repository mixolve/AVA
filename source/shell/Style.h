#pragma once

#include "Metrics.h"
#include "../modules/shared/ParameterFormatting.h"

#include <JuceHeader.h>

inline constexpr int initialEditorWidth = 340;
inline constexpr int minimumEditorWidth = 340;
inline constexpr int maximumEditorWidth = 4096;
inline constexpr int initialEditorHeight = 340;
inline constexpr int minimumEditorHeight = minimumEditorWidth;
inline constexpr int maximumEditorHeight = 4096;
inline constexpr int parameterGap = uiGap;
inline constexpr int verticalGap = uiGap;
inline constexpr int moduleContentBottomGap = verticalGap;
inline constexpr int globalToFilterGap = uiGap;
inline constexpr int rowHeight = 30;
inline constexpr float uiFontSize = 22.0f;

inline const auto uiWhite = juce::Colour(0xffffffff);
inline const auto uiGreyLight = juce::Colour(0xffbbbbbb);
inline const auto uiGreyDark = juce::Colour(0xff444444);
inline const auto uiBlack = juce::Colour(0xff000000);
inline const auto uiBackground = juce::Colour(0xff222222);
// Construct each colour independently: cross-TU dynamic initialisation can
// otherwise copy a not-yet-initialised inline colour in Windows builds.
inline const auto uiAccent = juce::Colour(0xffffffff);
inline const auto uiPopup = juce::Colour(0xff444444);

#include "ControlAppearance.h"

juce::Typeface::Ptr getUiTypeface();
juce::FontOptions makeUiFontOptions();
juce::Font makeUiFont();
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
juce::Colour getDisplayTextColour(const juce::String& text);
bool tryParseNoteFrequency(const juce::String& text, double& frequency);
double parseNumericInput(const juce::String& text);
double parseFrequencyInput(const juce::String& text);
bool supportsNoteFrequencyInput(const juce::String& parameterId);
double findNearestChoiceIndex(double targetValue, const juce::StringArray& choices, const juce::String& enteredText);
void clearKeyboardFocus(juce::Component& component);
bool isMouseHovering(const juce::Component& component) noexcept;
int getScaledParameterNameWidth(int rowWidth) noexcept;
