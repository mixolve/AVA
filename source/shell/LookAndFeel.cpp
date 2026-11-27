#include "Editor.h"
#include "LookAndFeel.h"

namespace
{
constexpr int popupItemHeight = 30;

}

AvaAudioProcessorEditor::AvaLookAndFeel::AvaLookAndFeel()
{
    if (auto typeface = getUiTypeface())
        setDefaultSansSerifTypeface(typeface);

    // Menu windows are created on the desktop before their opacity is decided.
    // An opaque popup colour makes JUCE recreate the heavyweight window as opaque,
    // and macOS shows that fresh window white for a frame before the first paint.
    // A transparent registered colour keeps the window non-opaque; the menu
    // background is still painted fully opaque by drawPopupMenuBackgroundWithOptions.
    setColour(juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
}

juce::Typeface::Ptr AvaAudioProcessorEditor::AvaLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    if (auto typeface = getUiTypeface())
        return typeface;
    return LookAndFeel_V4::getTypefaceForFont(font);
}

juce::Font AvaAudioProcessorEditor::AvaLookAndFeel::getPopupMenuFont()
{
    return makeUiFont();
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& graphics,
                                                                                 const int width,
                                                                                 const int height,
                                                                                 const juce::PopupMenu::Options& options)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                                static_cast<float>(width), static_cast<float>(height));
    const juce::Graphics::ScopedSaveState saved(graphics);
    const auto* target = options.getTargetComponent();
    const auto radius = float(defaultUiCornerRadius);
    if (radius > 0) graphics.reduceClipRegion(makeUiCornerPath(bounds, radius));
    if (target != nullptr) paintPluginBackground(graphics);
    else graphics.fillAll(uiGreyLight);
    paintUiFrame(graphics, bounds, uiWhite, 1.0f, radius);
}

int AvaAudioProcessorEditor::AvaLookAndFeel::getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&)
{
    // Items own their frames; an outer inset adds a third pixel beside hover.
    return 0;
}

void AvaAudioProcessorEditor::AvaLookAndFeel::getIdealPopupMenuItemSizeWithOptions(const juce::String& text,
                                                                                   bool isSeparator,
                                                                                   int,
                                                                                   int& idealWidth,
                                                                                   int& idealHeight,
                                                                                   const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 0;
        idealHeight = 2;
        return;
    }

    idealWidth = juce::jmax(80, getTextPixelWidth(makeUiFont(), text) + uiGapDouble);
    idealHeight = popupItemHeight;
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                                                const juce::Rectangle<int>& area,
                                                                bool isSeparator,
                                                                bool isActive,
                                                                bool isHighlighted,
                                                                bool isTicked,
                                                                bool hasSubMenu,
                                                                const juce::String& text,
                                                                const juce::String& shortcutKeyText,
                                                                const juce::Drawable* icon,
                                                                const juce::Colour* textColour)
{
    juce::ignoreUnused(hasSubMenu, shortcutKeyText, icon, textColour);

    if (isSeparator)
    {
        g.setColour(uiGreyLight);
        g.fillRect(area.withHeight(1).withCentre(area.getCentre()));
        return;
    }

    const auto itemBounds = area.withSizeKeepingCentre(area.getWidth(),
                                                       juce::jmin(popupItemHeight, area.getHeight()));
    ControlAppearance style;
    style.paint(g,itemBounds);
    paintControlTextButton(g, itemBounds, isActive && isHighlighted, isActive && isTicked, false,
        isActive ? uiGreyLight : uiIndicatorDisabled);
    g.setFont(makeUiFont());
    g.drawText(text, itemBounds.reduced(uiGap, 0), juce::Justification::centred, false);
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawPopupMenuItemWithOptions(juce::Graphics& g,
                                                                           const juce::Rectangle<int>& area,
                                                                           bool isHighlighted,
                                                                           const juce::PopupMenu::Item& item,
                                                                           const juce::PopupMenu::Options& options)
{
    juce::ignoreUnused(options);
    drawPopupMenuItem(g, area, item.isSeparator, item.isEnabled, isHighlighted, item.isTicked,
                      item.subMenu != nullptr, item.text, {}, nullptr, nullptr);
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawTextEditorOutline(juce::Graphics& g,int width,int height,juce::TextEditor& editor)
{
    if(!editor.isEnabled()) return;
    ControlAppearance style;
    style.frameColour=editor.findColour(juce::TextEditor::outlineColourId);
    style.paintFrame(g,juce::Rectangle<int>(0,0,width,height));
}

void AvaAudioProcessorEditor::AvaLookAndFeel::drawScrollbar(juce::Graphics& g,
    juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
    int thumbStart, int thumbSize, bool hovered, bool held)
{
    const juce::Graphics::ScopedSaveState saved(g);
    const auto* customBar = dynamic_cast<const CursorDragScrollBar*>(&bar);
    if (customBar != nullptr && customBar->headerRegions)
    {
        const auto regions = customBar->headerRegions();
        if (regions.hideThumb && !held) return;
        constexpr auto headerBarHeight = editorScrollBarWidth;
        constexpr auto inset = controlBlockPadding + controlBlockFrameThickness;
        const auto trackWidth = juce::jmax(0, width - 2 * inset);
        if (trackWidth == 0) return;
        const auto scale = double(trackWidth) / width;
        const auto thumbEnd = juce::roundToInt((thumbStart + thumbSize) * scale);
        thumbStart = juce::roundToInt(thumbStart * scale);
        thumbSize = thumbEnd - thumbStart;
        x += inset;
        width = trackWidth;
        y += (height - headerBarHeight) / 2;
        height = headerBarHeight;
        g.excludeClipRegion(regions.title.expanded(getUiGap(bar), 0));
        if (!regions.bypass.isEmpty()) g.excludeClipRegion(regions.bypass.expanded(getUiGap(bar), 0));
    }
    else
    {
        g.setColour(uiPanelBackground);
        g.fillRect(x, y, width, height);
    }
    const auto until = double(bar.getProperties().getWithDefault("scrollActiveUntil", 0.0));
    const bool scrolling = juce::Time::getMillisecondCounterHiRes() < until;
    const auto delayedHover = isHoverInversionReady(bar, hovered && !held && !scrolling, "scrollbar");
    const auto desired = held || scrolling ? uiWhite : delayedHover ? uiGreyLight : uiGreyDark;
    auto& state = bar.getProperties();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    constexpr double fadeMs = 50.0;
    const auto decode = [](const juce::var& value) { return juce::Colour(juce::uint32(juce::int64(value))); };
    auto target = decode(state.getWithDefault("scrollFadeTarget", juce::int64(desired.getARGB())));
    auto from = decode(state.getWithDefault("scrollFadeFrom", juce::int64(target.getARGB())));
    auto started = double(state.getWithDefault("scrollFadeStarted", now - fadeMs));
    auto colour = from.interpolatedWith(target, float(juce::jlimit(0.0, 1.0, (now - started) / fadeMs)));
    if (target != desired)
    {
        from = colour;
        target = desired;
        started = now;
        state.set("scrollFadeUntil", now + fadeMs);
    }
    state.set("scrollFadeFrom", juce::int64(from.getARGB()));
    state.set("scrollFadeTarget", juce::int64(target.getARGB()));
    state.set("scrollFadeStarted", started);
    // Interpolate opaque colours, preserving the solid scrollbar surface.
    colour = from.interpolatedWith(target, float(juce::jlimit(0.0, 1.0, (now - started) / fadeMs)));
    if (scrolling || double(state.getWithDefault("scrollFadeUntil", 0.0)) > now)
        trackScrollbarAnimation(bar);
    g.setColour(colour);
    if (vertical) g.fillRect(x, y + thumbStart, width, thumbSize);
    else g.fillRect(x + thumbStart, y, thumbSize, height);
}

void AvaAudioProcessorEditor::AvaLookAndFeel::trackScrollbarAnimation(juce::ScrollBar& bar)
{
    const auto containsBar = [&bar](const auto& item) { return item.getComponent() == &bar; };
    if (std::none_of(animatedScrollbars.begin(), animatedScrollbars.end(), containsBar))
        animatedScrollbars.emplace_back(&bar);
    if (!isTimerRunning()) startTimerHz(60);
}

void AvaAudioProcessorEditor::AvaLookAndFeel::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    std::erase_if(animatedScrollbars, [now](const auto& item)
    {
        auto* bar = item.getComponent();
        if (bar == nullptr || !bar->isShowing()) return true;
        auto& state = bar->getProperties();
        const auto scrollUntil = double(state.getWithDefault("scrollActiveUntil", 0.0));
        const auto fadeUntil = double(state.getWithDefault("scrollFadeUntil", 0.0));
        if (scrollUntil <= now) state.remove("scrollActiveUntil");
        if (fadeUntil <= now) state.remove("scrollFadeUntil");
        bar->repaint();
        return scrollUntil <= now && fadeUntil <= now;
    });
    if (animatedScrollbars.empty()) stopTimer();
}
