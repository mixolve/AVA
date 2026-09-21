#include "Style.h"
#include "Editor.h"

#include <cmath>
#include <limits>

int getEditorInsetX(const int width)
{
    juce::ignoreUnused(width);
    return uiGap;
}

int getEditorInsetTop(const int height)
{
    juce::ignoreUnused(height);
    return uiGap;
}

int getEditorInsetBottom(const int height)
{
    juce::ignoreUnused(height);
    return uiGap;
}

bool isMouseHovering(const juce::Component& component) noexcept
{
    const auto& desktop = juce::Desktop::getInstance();

    if (desktop.getNumDraggingMouseSources() > 0)
        return false;

    for (const auto& source : desktop.getMouseSources())
    {
        const auto* target = source.getComponentUnderMouse();
        if (source.isMouse() && (target == &component || (target != nullptr && component.isParentOf(target))))
            return true;
    }

    return false;
}

juce::Typeface::Ptr getUiTypeface()
{
#if JUCE_TARGET_HAS_BINARY_DATA
    static const auto typeface = juce::Typeface::createSystemTypefaceFor(BinaryData::IosevkaCharonMonoMedium_ttf,
                                                                         BinaryData::IosevkaCharonMonoMedium_ttfSize);
    return typeface;
#else
    return {};
#endif
}

juce::FontOptions makeUiFontOptions()
{
#if JUCE_TARGET_HAS_BINARY_DATA
    if (auto typeface = getUiTypeface())
        return juce::FontOptions(typeface).withHeight(uiFontSize);
#endif

    return juce::FontOptions("Iosevka Charon Mono", "Medium", uiFontSize);
}

juce::Font makeUiFont()
{
    return juce::Font(makeUiFontOptions());
}

int getTextPixelWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()));
}

bool drawLoopingText(juce::Graphics& graphics,
                     const juce::String& text,
                     const juce::Rectangle<int>& bounds,
                     const juce::Font& font,
                     const juce::Justification justification)
{
    if (text.isEmpty() || bounds.isEmpty())
        return false;

    const auto textWidth = static_cast<float>(getTextPixelWidth(font, text));

    if (textWidth <= static_cast<float>(bounds.getWidth()))
    {
        graphics.drawText(text, bounds, justification, false);
        return false;
    }

    constexpr auto pixelsPerSecond = 32.0f;
    const auto loopText = text + " : ";
    const auto loopWidth = static_cast<float>(getTextPixelWidth(font, loopText));
    const auto elapsedSeconds = static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.001);
    const auto offset = std::fmod(elapsedSeconds * pixelsPerSecond, loopWidth);
    const auto textY = static_cast<float>(bounds.getY());
    const auto textHeight = static_cast<float>(bounds.getHeight());
    const auto firstX = static_cast<float>(bounds.getX()) - offset;

    graphics.saveState();
    graphics.reduceClipRegion(bounds);
    graphics.drawText(loopText,
                      juce::Rectangle<float>(firstX, textY, loopWidth + 1.0f, textHeight),
                      juce::Justification::centredLeft,
                      false);
    graphics.drawText(loopText,
                      juce::Rectangle<float>(firstX + loopWidth, textY, loopWidth + 1.0f, textHeight),
                      juce::Justification::centredLeft,
                      false);
    graphics.restoreState();
    return true;
}

juce::String formatFixedDecimalValue(const double value, const int decimalPlaces)
{
    const auto places = juce::jmax(0, decimalPlaces);
    const auto scale = std::pow(10.0, static_cast<double>(places));
    auto roundedValue = std::round(value * scale) / scale;

    if (std::abs(roundedValue) < (0.5 / scale))
        roundedValue = 0.0;

    return juce::String(roundedValue, places);
}

juce::Colour getDisplayTextColour(const juce::String& text)
{
    return text.equalsIgnoreCase("OFF") || text.equalsIgnoreCase("LR")
        ? uiGrey500
        : uiWhite;
}

bool tryParseNoteFrequency(const juce::String& text, double& frequency)
{
    auto trimmed = text.trim().removeCharacters(" \t");

    if (trimmed.isEmpty())
        return false;

    const auto firstChar = static_cast<juce::juce_wchar>(juce::CharacterFunctions::toLowerCase(trimmed[0]));

    int semitone = 0;

    switch (firstChar)
    {
        case 'c': semitone = 0; break;
        case 'd': semitone = 2; break;
        case 'e': semitone = 4; break;
        case 'f': semitone = 5; break;
        case 'g': semitone = 7; break;
        case 'a': semitone = 9; break;
        case 'b': semitone = 11; break;
        default: return false;
    }

    int position = 1;

    if (position < trimmed.length())
    {
        const auto accidental = trimmed[position];

        if (accidental == '#' || accidental == 0x266f)
        {
            ++semitone;
            ++position;
        }
        else if (accidental == 'b' || accidental == 0x266d)
        {
            --semitone;
            ++position;
        }
    }

    if (position >= trimmed.length())
        return false;

    const auto octaveText = trimmed.substring(position);

    if (! octaveText.containsOnly("+-0123456789"))
        return false;

    const auto octave = octaveText.getIntValue();
    const auto midiNote = ((octave + 1) * 12) + semitone;
    frequency = 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
    return std::isfinite(frequency) && frequency > 0.0;
}

double parseNumericInput(const juce::String& text)
{
    const auto trimmed = text.trim();
    const auto numericValue = trimmed
        .retainCharacters("0123456789+-.")
        .getDoubleValue();

    if (trimmed.containsAnyOf("kKкК"))
        return numericValue * 1000.0;

    return numericValue;
}

double parseFrequencyInput(const juce::String& text)
{
    double noteFrequency = 0.0;

    if (tryParseNoteFrequency(text, noteFrequency))
        return noteFrequency;

    return parseNumericInput(text);
}

bool supportsNoteFrequencyInput(const juce::String& parameterId)
{
    return parameterId.endsWith("_frequency");
}

double findNearestChoiceIndex(const double targetValue, const juce::StringArray& choices, const juce::String& enteredText)
{
    const auto trimmedText = enteredText.trim();

    for (int index = 0; index < choices.size(); ++index)
    {
        if (trimmedText.equalsIgnoreCase(choices[index].trim()))
            return static_cast<double>(index);
    }

    auto bestIndex = 0;
    auto bestDistance = std::numeric_limits<double>::max();

    for (int index = 0; index < choices.size(); ++index)
    {
        const auto distance = std::abs(parseNumericInput(choices[index]) - targetValue);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = index;
        }
    }

    return static_cast<double>(bestIndex);
}

void clearKeyboardFocus(juce::Component& component)
{
    if (auto* focusedComponent = juce::Component::getCurrentlyFocusedComponent())
        focusedComponent->giveAwayKeyboardFocus();

    if (auto* topLevel = component.getTopLevelComponent())
        topLevel->unfocusAllComponents();

    if (auto* editor = component.findParentComponentOfClass<AvaAudioProcessorEditor>())
        editor->grabKeyboardFocus();
    else
        component.grabKeyboardFocus();
}

int getScaledParameterNameWidth(const int rowWidth) noexcept
{
    const auto usableWidth = juce::jmax(0, rowWidth - parameterGap);
    return usableWidth / 2;
}
