#pragma once

#include "UiConstants.h"

#include <JuceHeader.h>

inline constexpr int initialEditorWidth = 420;
inline constexpr int minimumEditorWidth = initialEditorWidth;
inline constexpr int maximumEditorWidth = initialEditorWidth;
inline constexpr int maximumStoredEditorWidth = 4096;
inline constexpr int initialEditorHeight = 650;
inline constexpr int minimumEditorHeight = minimumEditorWidth;
inline constexpr int maximumEditorHeight = 4096;
inline constexpr int parameterGap = uiGap;
inline constexpr int verticalGap = uiGap;
inline constexpr int moduleContentBottomGap = verticalGap;
inline constexpr int viewportToPotentiometerGap = verticalGap;
inline constexpr int footerHeight = 30;
inline constexpr int globalToFilterGap = uiGap;
inline constexpr int addFilterToFooterGap = uiGap;
inline constexpr int rowHeight = 30;
inline constexpr int fftInlineAnalyserHeight = rowHeight * fftInlineAnalyserHeightRows;
inline constexpr int presetRowGap = uiGap;
inline constexpr float uiFontSize = 22.0f;

inline const auto uiWhite = juce::Colour(0xffffffff);
inline const auto uiGreyLight = juce::Colour(0xffbbbbbb);
inline const auto uiGreyDark = juce::Colour(0xff444444);
inline const auto uiBlack = juce::Colour(0xff000000);
inline const auto uiAccent = uiWhite;
inline const auto uiClip = uiWhite;
inline const auto uiPopup = uiGreyDark;
inline const auto uiGrey800 = uiGreyDark;
inline const auto uiGrey500 = uiGreyLight;

int getEditorInsetX(int width);
int getEditorInsetTop(int height);
int getEditorInsetBottom(int height);
juce::Typeface::Ptr getUiTypeface();
juce::FontOptions makeUiFontOptions();
juce::Font makeUiFont();
int getTextPixelWidth(const juce::Font& font, const juce::String& text);
bool drawLoopingText(juce::Graphics& graphics,
                     const juce::String& text,
                     const juce::Rectangle<int>& bounds,
                     const juce::Font& font,
                     juce::Justification justification = juce::Justification::centred);
juce::String formatFixedDecimalValue(double value, int decimalPlaces);
juce::Colour getDisplayTextColour(const juce::String& text);
bool tryParseNoteFrequency(const juce::String& text, double& frequency);
double parseNumericInput(const juce::String& text);
double parseFrequencyInput(const juce::String& text);
bool supportsNoteFrequencyInput(const juce::String& parameterId);
double findNearestChoiceIndex(double targetValue, const juce::StringArray& choices, const juce::String& enteredText);
void clearKeyboardFocus(juce::Component& component);
bool isMouseHovering(const juce::Component& component) noexcept;
int getScaledParameterNameWidth(int rowWidth) noexcept;
