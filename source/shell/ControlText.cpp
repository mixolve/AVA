#include <list>
#include "Style.h"
#include <BinaryData.h>
#include "Editor.h"
#include "EditorStateSchema.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include "ValueEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool drawCompactControlText(juce::Graphics& graphics, const juce::String& text,
                            juce::Rectangle<int> bounds, juce::Justification justification)
{
    if (bounds.isEmpty()) return false;
    if (text.isEmpty())
    {
        return false;
    }

    // Render the same UI typeface as in 55, with its real glyph outlines.
    // Do not rely on Graphics::drawText fitting or character-width guesses:
    // either can clip three-letter names inside a 36px compact control.
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(makeUiFont(), text.trim(), 0.0f, 0.0f);
    juce::Path ink;
    glyphs.createPath(ink);
    const auto shape = ink.getBounds();
    if (shape.isEmpty()) return false;

    const auto zone = bounds.toFloat().reduced(1.0f);
    if (zone.isEmpty()) return false;
    const auto scale = juce::jmin(1.0f,
        juce::jmin(zone.getWidth() / shape.getWidth(), zone.getHeight() / shape.getHeight()));
    const auto width = shape.getWidth() * scale;
    const auto height = shape.getHeight() * scale;
    float x = zone.getCentreX() - width * 0.5f;
    if (justification.testFlags(juce::Justification::left)) x = zone.getX();
    if (justification.testFlags(juce::Justification::right)) x = zone.getRight() - width;
    const auto y = zone.getCentreY() - height * 0.5f;

    const juce::Graphics::ScopedSaveState saved(graphics);
    graphics.reduceClipRegion(bounds);
    graphics.fillPath(ink, juce::AffineTransform::scale(scale)
        .translated(x - shape.getX() * scale, y - shape.getY() * scale));
    return false;
}

void drawMultilineStatusText(juce::Graphics& graphics, const juce::String& source,
                             juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty()) return;
    const juce::Graphics::ScopedSaveState saved(graphics);
    if (!graphics.reduceClipRegion(bounds)) return;

    const auto lines = juce::StringArray::fromLines(source.trim());
    if (lines.isEmpty()) return;

    const auto font = makeUiFont();
    const auto textLineHeight = float(bounds.getHeight()) / float(lines.size());
    for (int index = 0; index < lines.size(); ++index)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, lines[index].trim(), 0.0f, 0.0f);
        juce::Path ink;
        glyphs.createPath(ink);
        const auto shape = ink.getBounds();
        if (shape.isEmpty()) continue;

        const auto maxWidth = juce::jmax(1.0f, float(bounds.getWidth()) - 4.0f);
        const auto maxHeight = juce::jmax(1.0f, textLineHeight - 2.0f);
        const auto scale = juce::jmin(1.0f,
            juce::jmin(maxWidth / shape.getWidth(), maxHeight / shape.getHeight()));
        const auto x = float(bounds.getCentreX()) - shape.getWidth() * scale * 0.5f;
        const auto y = float(bounds.getY()) + (float(index) + 0.5f) * textLineHeight
            - shape.getHeight() * scale * 0.5f;
        graphics.fillPath(ink, juce::AffineTransform::scale(scale)
            .translated(x - shape.getX() * scale, y - shape.getY() * scale));
    }
}

juce::String visibleControlCaption(const juce::String& source)
{
    auto text = source.trim().replaceCharacter('\r', ' ');
    if (source.containsChar('\n')) return text;
    bool containsLetters = false;
    for (const auto character : text)
        containsLetters = containsLetters || juce::CharacterFunctions::isLetter(character);
    if (containsLetters && text.indexOfChar('-') > 0)
        for (int index = 1; index < text.length(); ++index)
            if (text[index] == '-')
            {
                const auto numericSign = index + 1 < text.length()
                    && (juce::CharacterFunctions::isDigit(text[index + 1]) || text[index + 1] == '.')
                    && (juce::CharacterFunctions::isWhitespace(text[index - 1]) || text[index - 1] == '-');
                if (!numericSign) text = text.replaceSection(index, 1, " ");
            }
    return text;
}

bool drawSquareControlText(juce::Graphics& graphics, const juce::String& source,
                           juce::Rectangle<int> bounds, juce::Justification justification)
{
    if (bounds.isEmpty()) return false;
    const juce::Graphics::ScopedSaveState saved(graphics);
    if (!graphics.reduceClipRegion(bounds)) return false;
    const auto font = makeUiFont();
    const auto lineHeight = juce::jmax(1, juce::roundToInt(font.getHeight()));
    const auto maxLines = juce::jlimit(1, 2, bounds.getHeight() / lineHeight);
    const auto explicitLines = source.containsChar('\n');
    auto text = visibleControlCaption(source);
    const auto compound = source.containsChar('-') && source.indexOfChar('-') > 0;
    const auto words = juce::StringArray::fromTokens(text, " ", "");
    if (!explicitLines && maxLines > 1 && words.size() > 1 && (compound || getTextPixelWidth(font, text) > bounds.getWidth()))
    {
        auto bestWidth = std::numeric_limits<int>::max();
        auto bestSplit = 1;
        for (int split = 1; split < words.size(); ++split)
        {
            const auto width = juce::jmax(getTextPixelWidth(font, words.joinIntoString(" ", 0, split)),
                getTextPixelWidth(font, words.joinIntoString(" ", split, words.size() - split)));
            if (width < bestWidth) { bestWidth = width; bestSplit = split; }
        }
        text = words.joinIntoString(" ", 0, bestSplit) + "\n"
            + words.joinIntoString(" ", bestSplit, words.size() - bestSplit);
    }
    const auto lines = juce::StringArray::fromLines(text);
    const auto count = juce::jmin(maxLines, lines.size());
    graphics.setFont(font);
    auto y = bounds.getCentreY() - count * lineHeight / 2;
    bool scrolling = false;
    for (int index = 0; index < count; ++index)
    {
        scrolling = drawLoopingText(graphics, lines[index], bounds.withY(y).withHeight(lineHeight),
                                    font, justification) || scrolling;
        y += lineHeight;
    }
    return scrolling;
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
    struct Measurement { juce::Font font; juce::String text; int width; };
    // Measuring also shapes the text. Share stable measurements across controls
    // while keeping numeric edits and arbitrary labels bounded per UI thread.
    static thread_local std::list<Measurement> measurements;
    static thread_local size_t characters = 0;
    const auto found = std::find_if(measurements.begin(), measurements.end(), [&font, &text](const auto& item) {
        return item.text == text && item.font == font;
    });
    if (found != measurements.end()) {
        const auto width = found->width;
        measurements.splice(measurements.end(), measurements, found);
        return width;
    }
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    const auto width = static_cast<int>(std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()));
    const auto length = size_t(text.length());
    if (length <= 4096) {
        while (!measurements.empty() && (measurements.size() >= 512 || characters + length > 65536)) {
            characters -= size_t(measurements.front().text.length());
            measurements.pop_front();
        }
        measurements.push_back({font, text, width});
        characters += length;
    }
    return width;
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

bool drawEmbeddedButtonText(juce::Graphics& g,const juce::String& text,juce::Rectangle<int> bounds, juce::Colour colour)
{
    if (bounds.isEmpty()) return false;
    g.setColour(colour);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(makeUiFont(), text.trim(), 0.0f, 0.0f);
    juce::Path ink;
    glyphs.createPath(ink);
    const auto shape = ink.getBounds();
    if (!shape.isEmpty())
    {
        const auto zone = bounds.reduced(1).toFloat();
        juce::GlyphArrangement reference;
        reference.addLineOfText(makeUiFont(), "H", 0.0f, 0.0f);
        juce::Path capInk;
        reference.createPath(capInk);
        const auto cap = capInk.getBounds();
        const auto scale = zone.getHeight() / cap.getHeight();
        const auto width = shape.getWidth() * scale;
        const auto overflow = width > zone.getWidth();
        const auto period = width + 8.0f;
        const auto offset = overflow ? float(std::fmod(juce::Time::getMillisecondCounterHiRes() * 0.024, double(period))) : 0.0f;
        const auto x = overflow ? zone.getX() - offset : zone.getCentreX() - width * 0.5f;
        const auto y = zone.getCentreY() - cap.getCentreY() * scale;
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(bounds.reduced(1));
        g.fillPath(ink, juce::AffineTransform::scale(scale).translated(x - shape.getX() * scale, y));
        if (overflow)
        {
            g.fillPath(ink, juce::AffineTransform::scale(scale).translated(x + period - shape.getX() * scale, y));
            return true;
        }
    }
    return false;
}
