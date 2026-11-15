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

juce::Path makeUiCornerPath(const juce::Rectangle<float> bounds, const float radius)
{
    const auto requested = radius;
    const auto maximum = juce::jmax(0.0f, juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f);
    juce::Path path;
    path.addRoundedRectangle(bounds, juce::jlimit(0.0f, maximum, requested));
    return path;
}

void paintUiFrame(juce::Graphics& graphics, const juce::Rectangle<float> bounds,
                  const juce::Colour colour, const float thickness, const float radius)
{
    if (bounds.isEmpty()) return;
    const auto stroke = juce::jmax(0.01f, thickness);
    graphics.setColour(colour);
    graphics.strokePath(makeUiCornerPath(bounds.reduced(stroke * 0.5f),
        juce::jmax(0.0f, radius - stroke * 0.5f)),
                        juce::PathStrokeType(stroke));
}

void paintPluginBackground(juce::Graphics& graphics)
{
    graphics.fillAll(uiPanelBackground);
}

int getEditorContentInset(const juce::Component&) noexcept
{
    return uiGapDouble;
}

int getDockLength(const bool vertical, const int controls) noexcept
{
    // The collapsed dock has no editor scrollbar. Reserving its width here
    // leaves a redundant 20 px after the service buttons (right/bottom).
    return 2 * uiGap
        + controls * squareControlSize + (controls - 1) * uiGap
        + 2 * (controlBlockPadding + controlBlockFrameThickness) + (vertical ? uiGap : 0);
}

int getEditorWidth(const juce::Component& component) noexcept
{
    if (const auto* editor = dynamic_cast<const AvaAudioProcessorEditor*>(&component))
        if (editor->isDockOnly())
            return bool(editor->getRootWindowState().getProperty(shell_editor_ui::key("dock_vertical"), false))
                ? squareControlSize + 2 * uiGap : getDockLength(false, shell_editor_ui::dockControlCount(editor->getRootWindowState()));
    return getExpandedEditorWidth();
}

int getExpandedEditorWidth() noexcept
{
    return editorScrollBarArea + 2 * uiGap
        + viewportControlColumns * squareControlSize + (viewportControlColumns - 1) * uiGap
        + 2 * (controlBlockPadding + controlBlockFrameThickness)
        // The white INSTANCE frame contains dark six-column sections. Reserve
        // a real standard gap on both sides instead of shrinking their paint.
        + 2 * (uiGap + controlBlockFrameThickness);

}

int getEditorHeight(const juce::Component& component) noexcept
{
    return getEditorWidth(component);
}

void paintControlFill(juce::Graphics& graphics, juce::Rectangle<int> bounds,
                      juce::Colour colour)
{
    if (bounds.isEmpty()) return;
    graphics.setColour(colour);
    graphics.fillRect(bounds);
}


void paintIndicator(juce::Graphics& graphics, juce::Rectangle<float> bounds, juce::Colour colour, float radius)
{
    graphics.setColour(colour);
    graphics.fillPath(makeUiCornerPath(bounds, radius));
}

std::vector<juce::Component*> getForegroundOccluders(juce::Component& root)
{
    std::vector<juce::Component*> result;
    const auto visit = [&] (const auto& self, juce::Component& parent) -> void
    {
        for (auto* child : parent.getChildren())
            if (child->isVisible())
            {
                if (bool(child->getProperties().getWithDefault("foregroundOccluder", false))) result.push_back(child);
                else self(self, *child);
            }
    };
    visit(visit, root);
    return result;
}

juce::Rectangle<int> getVisibleControlBounds(const juce::Component& component, const juce::Component& root)
{
    auto bounds = root.getLocalArea(&component, component.getLocalBounds()).getIntersection(root.getLocalBounds());
    if (&component == &root) return bounds;
    for (auto* parent = component.getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
    {
        bounds = bounds.getIntersection(root.getLocalArea(parent, parent->getLocalBounds()));
        if (parent == &root) break;
    }
    return bounds;
}

int getUiGap(const juce::Component&) noexcept
{
    return uiGap;
}

int getUiRowGap(const juce::Component& component) noexcept
{
    return getUiGap(component);
}

int getSquareControlSize(const juce::Component&) noexcept
{
    return squareControlSize;
}

int getCompactControlSize(const juce::Component& component) noexcept
{
    return (getSquareControlSize(component) - getUiGap(component)) / 2;
}


namespace
{
template <typename Place>
juce::Rectangle<int> visitControlGrid(const juce::Component& owner, juce::Rectangle<int> bounds,
                                       const std::vector<ControlGridCell>& cells, Place&& place)
{
    const auto size = getSquareControlSize(owner), gap = getUiGap(owner);
    const auto columns = juce::jmax(1, (bounds.getWidth() + gap) / (size + gap));
    struct Placement { juce::Component* component; int column, row, span; };
    std::vector<Placement> placements;
    int column = 0, row = 0;
    for (const auto& cell : cells)
    {
        if (cell.component == nullptr) continue;
        const auto span = juce::jlimit(1, columns, cell.span);
        if (column > 0 && (cell.startNewRow || column + span > columns)) { column = 0; ++row; }
        placements.push_back({cell.component, column, row, span});
        column += span;
    }
    juce::Rectangle<int> occupied;
    for (const auto& placement : placements)
    {
        const juce::Rectangle<int> field(bounds.getX() + placement.column * (size + gap),
            bounds.getY() + placement.row * (size + getUiRowGap(owner)), placement.span * size + (placement.span - 1) * gap, size);
        place(*placement.component, field);
        occupied = occupied.getUnion(field);
    }
    return occupied;
}
}

juce::Rectangle<int> measureControlGrid(const juce::Component& owner, juce::Rectangle<int> bounds,
                                        const std::vector<ControlGridCell>& cells)
{
    return visitControlGrid(owner, bounds, cells, [](auto&, auto) {});
}

juce::Rectangle<int> layoutControlGrid(const juce::Component& owner, juce::Rectangle<int> bounds,
                                       const std::vector<ControlGridCell>& cells)
{
    return visitControlGrid(owner, bounds, cells, [](auto& component, auto field) { component.setBounds(field); });
}

juce::Rectangle<int> getControlBlockFrameBounds(juce::Rectangle<int> content) noexcept
{
    if (content.isEmpty()) return {};
    content.setWidth(juce::jmax(content.getWidth(), minimumControlColumns * squareControlSize + (minimumControlColumns - 1) * uiGap));
    const auto frame = content.expanded(controlBlockFrameOutset);
    return frame.withTrimmedTop(-controlBlockHeaderHeight);
}

juce::Rectangle<int> getModuleBlockFrameBounds(juce::Rectangle<int> blocks) noexcept
{
    if (blocks.isEmpty()) return {};
    auto frame = blocks.expanded(controlBlockFrameOutset);
    frame.setTop(blocks.getY() - controlBlockHeaderHeight - uiGap);
    return frame;
}

void paintControlBlockFrame(juce::Graphics& graphics, juce::Rectangle<int> bounds, const juce::String& title,
                            juce::Colour colour, bool hovered, bool collapsed, bool centreInkVertically)
{
    if (bounds.isEmpty()) return;
    const auto header = bounds.withHeight(controlBlockHeaderHeight);
    // Both light and dark section headers use the same #BBBBBB hover.
    // Dark headers now rest at #444444; white headers remain white.
    const auto effectiveColour = hovered ? uiGreyLight
        : (colour == uiWhite ? uiWhite : uiGreyDark);
    graphics.setColour(effectiveColour);
    graphics.fillRect(header);
    graphics.setColour(effectiveColour == uiGreyDark ? uiWhite : juce::Colour(0xff222222));
    // Every section header (UNIT, INSTANCE, nested blocks, module blocks)
    // must share one optical baseline. Centring each caption by its own ink
    // bounds shifts uppercase letters when a title contains digits, brackets,
    // or a descender. Anchor the vertical centre to the uppercase cap height
    // instead, while continuing to centre the actual caption horizontally.
    const auto font = makeUiFont();
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, title.toUpperCase(), 0.0f, 0.0f);
    juce::Path ink;
    glyphs.createPath(ink);
    const auto inkBounds = ink.getBounds();
    static const auto capBounds = []
    {
        juce::GlyphArrangement reference;
        reference.addLineOfText(makeUiFont(), "H", 0.0f, 0.0f);
        juce::Path capInk;
        reference.createPath(capInk);
        return capInk.getBounds();
    }();
    const auto textArea = header.reduced(uiGap, 0).toFloat();
    if (!inkBounds.isEmpty() && !capBounds.isEmpty() && !textArea.isEmpty())
    {
        // Account for descenders/parentheses on both sides of the cap-height
        // centre so no glyph reaches beyond the header's 2px vertical inset.
        constexpr float verticalInset = 2.0f;
        const auto capCentreY = capBounds.getCentreY();
        const auto halfInkExtent = juce::jmax(
            std::abs(inkBounds.getY() - capCentreY),
            std::abs(inkBounds.getBottom() - capCentreY));
        const auto maxHalfHeight = juce::jmax(1.0f, textArea.getHeight() * 0.5f - verticalInset);
        const auto scale = juce::jmin(1.0f,
            juce::jmin(textArea.getWidth() / inkBounds.getWidth(),
                       maxHalfHeight / juce::jmax(0.001f, halfInkExtent)));
        const auto x = textArea.getCentreX() - inkBounds.getCentreX() * scale;
        const auto verticalCentre = centreInkVertically ? inkBounds.getCentreY() : capCentreY;
        const auto y = textArea.getCentreY() - verticalCentre * scale;
        graphics.fillPath(ink, juce::AffineTransform::scale(scale).translated(x, y));
    }
    // A collapsed block is only its header: no side or bottom border.
    if (collapsed) return;
    // Use solid pixel-aligned strips for identical white and dark borders.
    const auto thickness = juce::jmin(controlBlockFrameThickness, juce::jmin(bounds.getWidth(), bounds.getHeight()));
    graphics.setColour(effectiveColour);
    graphics.fillRect(bounds.withWidth(thickness));
    graphics.fillRect(bounds.withLeft(bounds.getRight() - thickness));
    graphics.fillRect(bounds.withHeight(thickness));
    graphics.fillRect(bounds.withTop(bounds.getBottom() - thickness));
}

bool isHoverInversionReady(juce::Component& component, const bool hovering, const char* region)
{
    // Only the paint state is delayed. Mouse gestures, focus and dragging
    // still run on their original synchronous handlers.
    const auto prefix = juce::String("ava.ui.hover.") + region + ".";
    const juce::Identifier requested(prefix + "requested");
    const juce::Identifier shown(prefix + "shown");
    const juce::Identifier generation(prefix + "generation");
    auto& properties = component.getProperties();
    if (!hovering)
    {
        if (bool(properties.getWithDefault(requested, false)))
        {
            properties.set(requested, false);
            properties.set(shown, false);
            // Discard a pending reveal even if the pointer returns immediately.
            properties.set(generation, int(properties.getWithDefault(generation, 0)) + 1);
        }
        return false;
    }
    if (!bool(properties.getWithDefault(requested, false)))
    {
        const int current = int(properties.getWithDefault(generation, 0)) + 1;
        properties.set(generation, current);
        properties.set(requested, true);
        properties.set(shown, false);
        juce::Timer::callAfterDelay(50,
            [safe = juce::Component::SafePointer<juce::Component>(&component),
             requested, shown, generation, current]
            {
                if (safe == nullptr) return;
                auto& state = safe->getProperties();
                if (bool(state.getWithDefault(requested, false))
                    && int(state.getWithDefault(generation, 0)) == current)
                {
                    state.set(shown, true);
                    repaintValueOverlay(*safe);
                }
            });
    }
    return bool(properties.getWithDefault(shown, false));
}

juce::Rectangle<int> getControlTextBounds(const juce::Component& component, juce::Rectangle<int> bounds) noexcept
{
    bool top = true, bottom = true, known = false;
    int innerSize = RangeIndicator::bottomControlSize(component, bounds);
    if (bool(component.getProperties().getWithDefault("passiveTextControl", false)))
    { top = bottom = false; known = true; }
    if (const auto* value = dynamic_cast<const ValueBoxComponent*>(&component))
    {
        if (const auto* field = value->findParentComponentOfClass<ParameterField>())
        {
            top = field->hasTopInternalControls(); bottom = field->hasBottomInternalControls();
            innerSize = field->getBottomIndicatorSize();
        }
        else { top = value->hasTopInternalControls(); bottom = value->hasBottomInternalControls(); }
        known = true;
    }
    else if (const auto* field = dynamic_cast<const ParameterField*>(&component))
    {
        top = field->hasTopInternalControls(); bottom = field->hasBottomInternalControls();
        innerSize = field->getBottomIndicatorSize(); known = true;
    }
    else if (const auto* button = dynamic_cast<const BoxTextButton*>(&component))
    { top = false; bottom = button->hasBottomInternalControls(); known = true; }
    const auto compact = bounds.getHeight() <= getCompactControlSize(component) + 2
        && bounds.getWidth() <= getCompactControlSize(component) + 2;
    if (compact && !known) top = bottom = false;
    constexpr int side = controlEdgeInset;
    const auto topInset = top ? controlEdgeInset + innerSize + controlInnerGap : controlEdgeInset;
    const auto bottomInset = bottom ? controlEdgeInset + innerSize + controlInnerGap : controlEdgeInset;
    auto text = juce::Rectangle<int>(bounds.getX() + side, bounds.getY() + topInset,
        juce::jmax(0, bounds.getWidth() - 2 * side), juce::jmax(0, bounds.getHeight() - topInset - bottomInset));
    if (component.getProperties().contains("textTopInset"))
        text.setTop(bounds.getY() + int(component.getProperties()["textTopInset"]));
    if (component.getProperties().contains("textBottomInset"))
        text.setBottom(bounds.getBottom() - int(component.getProperties()["textBottomInset"]));
    if (component.getProperties().contains("textSideInset"))
    {
        const auto inset = int(component.getProperties()["textSideInset"]);
        text.setLeft(bounds.getX() + inset); text.setRight(bounds.getRight() - inset);
    }
    return text;
}

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
    // Compact labels are centred like their 55 counterparts; preserve a
    // caller's explicit left/right justification where requested.
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

        // Keep the glyph proportions rather than horizontally squeezing text.
        // This also fits the frequency value when the UI is in compact mode.
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

bool isMouseHovering(const juce::Component& component) noexcept
{
    return isMouseHovering(component,component.getLocalBounds());
}

bool isMouseHovering(const juce::Component& component,juce::Rectangle<int> region) noexcept
{
    const auto& desktop = juce::Desktop::getInstance();

    if (desktop.getNumDraggingMouseSources() > 0)
        return false;

    for (const auto& source : desktop.getMouseSources())
    {
        if (! source.isMouse()) continue;
        // JUCE's cached target is updated by a later synthetic mouse event after
        // replacing/hiding a control. Hit-test the current tree for the first paint.
        const auto point=component.getLocalPoint(nullptr,source.getScreenPosition());
        if (region.toFloat().contains(point) && const_cast<juce::Component&>(component).reallyContains(point,true))
            return true;
    }

    return false;
}

bool isEmptyEditorBackground(const juce::Component* component)
{
    if(component==nullptr) return false;
    if(typeid(*component)==typeid(juce::Component) || dynamic_cast<const juce::Viewport*>(component)!=nullptr
        || dynamic_cast<const BackgroundClickComponent*>(component)!=nullptr
        || dynamic_cast<const AvaAudioProcessorEditor*>(component)!=nullptr
        || dynamic_cast<const ParameterControl*>(component)!=nullptr
        || dynamic_cast<const ChoiceControl*>(component)!=nullptr
        || dynamic_cast<const LocalChoiceControl*>(component)!=nullptr)
        return true;
    if(const auto* viewport=dynamic_cast<const juce::Viewport*>(component->getParentComponent()))
        if(viewport->getViewedComponent()!=nullptr && component==viewport->getViewedComponent()->getParentComponent())
            return true;
    return static_cast<bool>(component->getProperties().getWithDefault("emptyEditorBackground",false));
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
