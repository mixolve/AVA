#include "Style.h"
#include "Editor.h"

#include <cmath>
#include <limits>

bool isMouseHovering(const juce::Component& component) noexcept
{
    const auto& desktop = juce::Desktop::getInstance();

    if (desktop.getNumDraggingMouseSources() > 0)
        return false;

    for (const auto& source : desktop.getMouseSources())
    {
        if (! source.isMouse()) continue;
        // JUCE's cached target is updated by a later synthetic mouse event after
        // replacing/hiding a control. Hit-test the current tree for the first paint.
        if (const_cast<juce::Component&>(component).reallyContains(component.getLocalPoint(nullptr, source.getScreenPosition()), true))
            return true;
    }

    return false;
}

juce::Typeface::Ptr getUiTypeface()
{
#if JUCE_TARGET_HAS_BINARY_DATA
    static const auto typeface = juce::Typeface::createSystemTypefaceFor(BinaryData::IosevkaCharonMonoBold_ttf,
                                                                         BinaryData::IosevkaCharonMonoBold_ttfSize);
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

    return juce::FontOptions("Iosevka Charon Mono", "Bold", uiFontSize);
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
                     const juce::Justification justification,
                     const double elapsedSeconds,
                     const float verticalOffset)
{
    if (text.isEmpty() || bounds.isEmpty())
        return false;

    const auto drawText = [&](const juce::String& value, juce::Rectangle<float> area,
                              juce::Justification alignment)
    {
        if (verticalOffset == 0.0f)
        {
            graphics.drawText(value, area, alignment, false);
            return;
        }
        // Cached raster glyphs snap their baseline to whole pixels. Use the
        // same font outlines for optically centred readouts, so a fractional
        // vertical correction survives rasterisation.
        juce::GlyphArrangement glyphs;
        glyphs.addCurtailedLineOfText(font, value, 0.0f, 0.0f, area.getWidth(), false);
        glyphs.justifyGlyphs(0, glyphs.getNumGlyphs(), area.getX(), area.getY(),
                            area.getWidth(), area.getHeight(), alignment);
        juce::Path outlines;
        glyphs.createPath(outlines);
        graphics.fillPath(outlines, juce::AffineTransform::translation(0.0f, verticalOffset));
    };
    const auto textWidth = static_cast<float>(getTextPixelWidth(font, text));

    if (textWidth <= static_cast<float>(bounds.getWidth()))
    {
        drawText(text, bounds.toFloat(), justification);
        return false;
    }

    constexpr auto pixelsPerSecond = 32.0f;
    const auto loopText = text + " :: ";
    const auto loopWidth = static_cast<float>(getTextPixelWidth(font, loopText));
    const auto elapsed = static_cast<float>(elapsedSeconds >= 0.0
        ? elapsedSeconds : juce::Time::getMillisecondCounterHiRes() * 0.001);
    const auto offset = std::fmod(elapsed * pixelsPerSecond, loopWidth);
    const auto textY = static_cast<float>(bounds.getY());
    const auto textHeight = static_cast<float>(bounds.getHeight());
    const auto firstX = static_cast<float>(bounds.getX()) - offset;

    graphics.saveState();
    graphics.reduceClipRegion(bounds);
    drawText(loopText, {firstX, textY, loopWidth + 1.0f, textHeight}, juce::Justification::centredLeft);
    drawText(loopText, {firstX + loopWidth, textY, loopWidth + 1.0f, textHeight}, juce::Justification::centredLeft);
    graphics.restoreState();
    return true;
}

void scheduleMarqueeRepaint(juce::Component& component, bool& pending)
{
    if (pending || ! component.isShowing())
        return;

    pending = true;
    juce::Timer::callAfterDelay(16, [safeComponent = juce::Component::SafePointer<juce::Component>(&component), &pending]
    {
        if (safeComponent == nullptr)
            return;

        pending = false;
        safeComponent->repaint();
    });
}

juce::Colour getDisplayTextColour(const juce::String& text)
{
    return text.equalsIgnoreCase("OFF") || text.equalsIgnoreCase("LR")
        ? uiGreyLight
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
    return parameterId.endsWith("_freq");
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
