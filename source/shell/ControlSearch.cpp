#include "ControlSearch.h"
#include "ControlHelp.h"
#include "Editor.h"
#include "Style.h"

#include <utility>

namespace
{
constexpr int visibleRows = 4;
constexpr int resultPadding = controlBlockPadding;
constexpr int rowGap = uiGap;
constexpr auto dark = 0xff222222;
}

ControlSearchPanel::ControlSearchPanel() : input(*this), resultsCanvas(*this)
{
    setName("all-control-search");
    input.setName("SEARCH");
    resultsCanvas.setName("control-search-results");
    input.setMultiLine(false);
    input.setReturnKeyStartsNewLine(false);
    input.setScrollbarsShown(false);
    // Keep the native single-line caret and selection positions unchanged.
    // Glyphs are centred by their ink bounds in SearchTextEditor below.
    constexpr float searchTextHeight = 18.0f;
    input.setFont(makeUiFont().withHeight(searchTextHeight));
    input.setBorder(juce::BorderSize<int>(0));
    input.setIndents(0, 0);
    input.setJustification(juce::Justification::centred);
    input.setInputFilter(new UppercaseEditorInputFilter(), true);
    input.setColour(juce::TextEditor::backgroundColourId, uiWhite);
    // Native text is transparent, but the native highlight and caret remain
    // untouched. Only our overlay draws the visible ink, at the correct Y.
    input.setColour(juce::TextEditor::textColourId, juce::Colours::transparentBlack);
    input.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    input.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xffdddddd));
    input.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::transparentBlack);
    input.setColour(juce::CaretComponent::caretColourId, juce::Colour(dark));
    input.onTextChange = [this] { updateQuery(); };
    addAndMakeVisible(input);

    listViewport.setViewedComponent(&resultsCanvas, false);
    listViewport.setScrollBarsShown(true, false, false, false);
    listViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    addAndMakeVisible(listViewport);
}

bool ControlSearchPanel::SearchTextEditor::keyPressed(const juce::KeyPress& key)
{
    if (owner.helpText.isNotEmpty() || owner.handleSearchKey(key)) return true;
    return juce::TextEditor::keyPressed(key);
}

void ControlSearchPanel::SearchTextEditor::focusGained(FocusChangeType cause)
{
    juce::TextEditor::focusGained(cause);
    // Select the previous query once on entering search mode. Subsequent
    // clicks while editing must be allowed to position the text caret.
    setColour(juce::TextEditor::highlightColourId, uiPanelBackground);
    selectAll();
}

void ControlSearchPanel::SearchTextEditor::focusLost(FocusChangeType cause)
{
    juce::TextEditor::focusLost(cause);
    setColour(juce::TextEditor::highlightColourId, juce::Colour(0xffdddddd));
}

void ControlSearchPanel::SearchTextEditor::mouseDown(const juce::MouseEvent& event)
{
    // Only the click that gives the editor focus begins a new search session.
    // Subsequent clicks move the caret or start a text selection normally.
    selectOnMouseUp = !hasKeyboardFocus(true) && !event.mods.isPopupMenu();
    owner.reopenResults();
    juce::TextEditor::mouseDown(event);
}

void ControlSearchPanel::SearchTextEditor::mouseUp(const juce::MouseEvent& event)
{
    juce::TextEditor::mouseUp(event);
    // JUCE may collapse focusGained()'s selection during the initial click.
    // Restore it once, but never replace a user-made drag selection.
    if (std::exchange(selectOnMouseUp, false)
        && !event.mods.isPopupMenu() && !event.mouseWasDraggedSinceMouseDown())
        selectAll();
}

void ControlSearchPanel::SearchTextEditor::paintOverChildren(juce::Graphics& g)
{
    if (owner.helpText.isNotEmpty())
    {
        if (control_help::paint(g, owner.helpText, getLocalBounds(), owner.helpStartedAtMs))
            scheduleMarqueeRepaint(*this, helpMarqueePending);
        return;
    }
    juce::TextEditor::paintOverChildren(g);
    const auto value = getText();
    if (value.isEmpty()) return;

    // JUCE positions the selection rectangles and flashing caret correctly,
    // but its font baseline does not visually centre the uppercase ink. Use
    // native text bounds for the horizontal position (including scrolling)
    // and move ONLY the rendered glyph outlines along Y.
    const auto bounds = getTextBounds({0, value.length()});
    if (bounds.isEmpty()) return;
    juce::Rectangle<float> nativeTextArea;
    for (const auto& area : bounds)
        nativeTextArea = nativeTextArea.getUnion(area.toFloat());

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(getFont(), value, 0.0f, 0.0f);
    juce::Path ink;
    glyphs.createPath(ink);
    const auto inkBounds = ink.getBounds();
    if (inkBounds.isEmpty()) return;

    const auto x = nativeTextArea.getCentreX() - inkBounds.getCentreX();
    const auto y = getLocalBounds().toFloat().getCentreY() - inkBounds.getCentreY();
    const auto shift = juce::AffineTransform::translation(x, y);
    const juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(getLocalBounds());
    g.setColour(juce::Colour(0xff222222));
    g.fillPath(ink, shift);

    // Preserve the existing focused selection's light glyphs over its dark
    // native highlight without altering either selection or caret bounds.
    if (hasKeyboardFocus(true))
    {
        const auto selection = getHighlightedRegion();
        if (!selection.isEmpty())
            for (const auto& area : getTextBounds(selection))
            {
                const juce::Graphics::ScopedSaveState selectionClip(g);
                g.reduceClipRegion(area);
                g.setColour(uiWhite);
                g.fillPath(ink, shift);
            }
    }
}

bool ControlSearchPanel::handleSearchKey(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::downKey)
    {
        if (results.empty()) return true;
        selectRow(selectedRow < 0 ? 0 : juce::jmin(selectedRow + 1, int(results.size()) - 1));
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::upKey)
    {
        if (results.empty()) return true;
        selectRow(selectedRow < 0 ? int(results.size()) - 1 : juce::jmax(0, selectedRow - 1));
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        if (input.getText().trim().isEmpty())
            input.giveAwayKeyboardFocus();
        else
            chooseRow(selectedRow < 0 ? 0 : selectedRow);
        return true;
    }
    return false;
}

void ControlSearchPanel::chooseRow(int index)
{
    if (!juce::isPositiveAndBelow(index, int(results.size()))) return;
    const auto chosen = results[static_cast<size_t>(index)];
    // Mouse and Enter share one completion path. Dismiss BEFORE navigation,
    // since navigation can relayout and refocus components under the pointer.
    resultsDismissed = true;
    selectedRow = -1;
    resultsCanvas.resetHover();
    input.giveAwayKeyboardFocus();
    if (onDisplayChanged) onDisplayChanged();
    if (onChoose) onChoose(chosen);
    // Give focus back to the editor, not to an arbitrary child of the scroller.
    // Its Enter shortcut can now open search immediately after navigation.
    if (auto* editor = findParentComponentOfClass<AvaAudioProcessorEditor>())
        editor->grabKeyboardFocus();
}

void ControlSearchPanel::setHelpText(const juce::String& text)
{
    if (helpText == text) return;
    const auto previousHeight = getDisplayedHeight();
    const bool wasShowingHelp = helpText.isNotEmpty();
    const bool showingHelp = text.isNotEmpty();
    if (showingHelp && !wasShowingHelp)
        resultsScrollBeforeHelp = listViewport.getViewPosition();

    helpText = text;
    helpStartedAtMs = juce::Time::getMillisecondCounterHiRes();
    if (wasShowingHelp != showingHelp)
    {
        input.setReadOnly(showingHelp);
        input.setCaretVisible(!showingHelp);
        if (previousHeight != getDisplayedHeight() && onDisplayChanged)
            onDisplayChanged();
        if (!showingHelp)
            listViewport.setViewPosition(resultsScrollBeforeHelp);
    }
    input.repaint();
}

void ControlSearchPanel::focusSearch()
{
    if (!isShowing()) return;
    reopenResults();
    input.grabKeyboardFocus();
    input.selectAll();
}

void ControlSearchPanel::reopenResults()
{
    if (!resultsDismissed) return;
    resultsDismissed = false;
    refreshMatches();
}

void ControlSearchPanel::selectRow(int index)
{
    if (!juce::isPositiveAndBelow(index, int(results.size()))) return;
    selectedRow = index;
    const auto rowTop = index * (controlBlockHeaderHeight + rowGap);
    const auto rowBottom = rowTop + controlBlockHeaderHeight;
    const auto viewTop = listViewport.getViewPositionY();
    if (rowTop < viewTop) listViewport.setViewPosition(0, rowTop);
    else if (rowBottom > viewTop + listViewport.getHeight())
        listViewport.setViewPosition(0, rowBottom - listViewport.getHeight());
    resultsCanvas.repaint();
}

ControlSearchPanel::~ControlSearchPanel()
{
    listViewport.setViewedComponent(nullptr, false);
}

int ControlSearchPanel::getPreferredHeight() const noexcept
{
    // No results means just the search input: no empty results frame or
    // "NO MATCHES" placeholder, whether the search is pinned or not.
    const auto rows = juce::jmin(visibleRows, static_cast<int>(results.size()));
    if (rows == 0) return controlBlockHeaderHeight;
    return controlBlockHeaderHeight + 2 * resultPadding
        + rows * controlBlockHeaderHeight + (rows - 1) * rowGap;
}

int ControlSearchPanel::getDisplayedHeight() const noexcept
{
    return resultsDismissed || helpText.isNotEmpty() ? controlBlockHeaderHeight : getPreferredHeight();
}

void ControlSearchPanel::updateQuery()
{
    // InputFilter handles typing, paste and IME inserts. Normalise again here
    // to cover other TextEditor value/programmatic change paths as well.
    const auto original = input.getText();
    const auto normalised = uppercaseText(original);
    if (normalised != original)
    {
        const auto caret = input.getCaretPosition();
        const auto selection = input.getHighlightedRegion();
        input.setText(normalised, false);
        const auto length = normalised.length();
        if (!selection.isEmpty())
            input.setHighlightedRegion({juce::jlimit(0, length, selection.getStart()),
                                        juce::jlimit(0, length, selection.getEnd())});
        else
            input.setCaretPosition(juce::jlimit(0, length, caret));
    }
    resultsDismissed = false;
    refreshMatches();
}

void ControlSearchPanel::refreshMatches()
{
    results.clear();
    selectedRow = -1;
    resultsCanvas.resetHover();
    const auto query = uppercaseText(input.getText().trim());
    if (query.isNotEmpty() && findMatches)
        results = findMatches(query);
    resized();
    listViewport.setViewPosition(0, 0);
    resultsCanvas.repaint();
    if (onDisplayChanged) onDisplayChanged();
    repaint();
}

void ControlSearchPanel::resized()
{
    const auto width = getWidth();
    frame = {0, 0, width, getHeight()};

    // Keep the full-size native editing field: caret and selection geometry
    // must not be moved together with the custom centred glyphs.
    input.setBounds(frame.withHeight(controlBlockHeaderHeight));
    // Search results use the same insets as nested MAIN/module headers.
    const auto inset = uiGap + controlBlockFrameThickness;
    const auto area = getHeight() > controlBlockHeaderHeight
        ? juce::Rectangle<int> {inset, controlBlockHeaderHeight + resultPadding,
            juce::jmax(0, width - 2 * inset),
            juce::jmax(0, getHeight() - controlBlockHeaderHeight - 2 * resultPadding)}
        : juce::Rectangle<int> {};
    listViewport.setBounds(area);
    listViewport.setVisible(getHeight() > controlBlockHeaderHeight);
    const auto resultsHeight = resultsCanvas.getContentHeight();
    const auto barWidth = resultsHeight > area.getHeight() ? listViewport.getScrollBarThickness() : 0;
    resultsCanvas.setSize(juce::jmax(0, area.getWidth() - barWidth), resultsHeight);
}

void ControlSearchPanel::paint(juce::Graphics& g)
{
    g.setColour(uiPanelBackground);
    g.fillRect(frame.withTrimmedTop(controlBlockHeaderHeight));
    g.setColour(uiWhite);
    g.fillRect(input.getBounds());
}

void ControlSearchPanel::paintOverChildren(juce::Graphics& g)
{
    if (frame.isEmpty()) return;
    if (getHeight() > controlBlockHeaderHeight)
    {
        const auto resultsFrame = frame.withTrimmedTop(controlBlockHeaderHeight);
        const auto thickness = juce::jmin(controlBlockFrameThickness,
            juce::jmin(resultsFrame.getWidth(), resultsFrame.getHeight()));
        g.setColour(uiWhite);
        g.fillRect(resultsFrame.withWidth(thickness));
        g.fillRect(resultsFrame.withLeft(resultsFrame.getRight() - thickness));
        g.fillRect(resultsFrame.withTop(resultsFrame.getBottom() - thickness));
    }
}

int ControlSearchPanel::ResultCanvas::getContentHeight() const noexcept
{
    const auto count = static_cast<int>(owner.results.size());
    return count > 0 ? count * controlBlockHeaderHeight + (count - 1) * rowGap : 0;
}

int ControlSearchPanel::ResultCanvas::rowAt(juce::Point<int> p) const noexcept
{
    if (p.x < 0 || p.x >= getWidth() || p.y < 0) return -1;
    const auto stride = controlBlockHeaderHeight + rowGap;
    const auto index = p.y / stride;
    if (p.y % stride >= controlBlockHeaderHeight) return -1;
    return juce::isPositiveAndBelow(index, static_cast<int>(owner.results.size())) ? index : -1;
}

void ControlSearchPanel::ResultCanvas::mouseMove(const juce::MouseEvent& e)
{
    const auto selected = rowAt(e.getPosition());
    if (hoveredRow == selected) return;
    hoveredRow = selected;
    shownHoveredRow = -1;
    const auto generation = ++hoverGeneration;
    repaint();
    if (selected >= 0)
        juce::Timer::callAfterDelay(50,
            [safe = juce::Component::SafePointer<ResultCanvas>(this), selected, generation]
            {
                if (safe == nullptr || safe->hoverGeneration != generation
                    || safe->hoveredRow != selected) return;
                safe->shownHoveredRow = selected;
                safe->repaint();
            });
}

void ControlSearchPanel::ResultCanvas::mouseExit(const juce::MouseEvent&)
{
    if (hoveredRow != -1 || shownHoveredRow != -1)
    {
        hoveredRow = shownHoveredRow = -1;
        ++hoverGeneration;
        repaint();
    }
}

juce::MouseCursor ControlSearchPanel::ResultCanvas::getMouseCursor()
{
    return hoveredRow >= 0 ? juce::MouseCursor::PointingHandCursor
                           : juce::MouseCursor::NormalCursor;
}

void ControlSearchPanel::ResultCanvas::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown()) return;
    const auto selected = rowAt(e.getPosition());
    if (juce::isPositiveAndBelow(selected, static_cast<int>(owner.results.size())))
    {
        owner.selectRow(selected);
        owner.chooseRow(selected);
    }
}

void ControlSearchPanel::ResultCanvas::paint(juce::Graphics& g)
{
    const auto count = static_cast<int>(owner.results.size());
    for (int i = 0; i < count; ++i)
    {
        const auto bounds = juce::Rectangle<int>(0, i * (controlBlockHeaderHeight + rowGap),
                                                  getWidth(), controlBlockHeaderHeight);
        if (!g.getClipBounds().intersects(bounds)) continue;
        const auto selected = i < count && (shownHoveredRow == i || owner.selectedRow == i);
        g.setColour(selected ? juce::Colour(dark) : uiWhite);
        g.fillRect(bounds);
        g.setColour(selected ? uiWhite : juce::Colour(dark));
        const auto caption = owner.results[static_cast<size_t>(i)].location;
        g.setFont(makeUiFont());
        g.drawFittedText(caption, bounds.reduced(uiGap, 0), juce::Justification::centred,
                         1, 0.55f);
    }
}
