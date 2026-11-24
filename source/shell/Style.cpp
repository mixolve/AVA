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
    if (collapsed) return;
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

int getPinnedRoutingTileHeight(const juce::Component& component) noexcept
{
    const auto size = getSquareControlSize(component);
    return standardControlTextZoneHeight + 2 * controlEdgeInset + controlInnerGap
        + RangeIndicator::bottomControlSize(component, {0, 0, size, size});
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
