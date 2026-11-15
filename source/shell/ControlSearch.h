#pragma once

#include "Controls.h"

#include <functional>
#include <vector>

// A live, scoped search for controls and block captions on the ALL page.
struct ControlSearchEntry
{
    enum class Action { none, scrollTop, collapseAll };
    juce::String name;
    juce::String location;
    juce::Component::SafePointer<juce::Component> target;
    juce::String blockTitle; // Nonempty for a block-header result.
    Action action = Action::none;
};

class ControlSearchPanel final : public juce::Component
{
public:
    ControlSearchPanel();
    ~ControlSearchPanel() override;

    std::function<std::vector<ControlSearchEntry>(const juce::String&)> findMatches;
    std::function<void(ControlSearchEntry)> onChoose;

    // The search header stays visible; a results frame exists only for matches.
    void setPinned(bool pinned) noexcept;
    int getDisplayedHeight() const noexcept;
    int getPreferredHeight() const noexcept;
    std::function<void()> onDisplayChanged;
    void refreshMatches();
    void focusSearch();
    void resized() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;

private:
    class SearchTextEditor final : public juce::TextEditor
    {
    public:
        explicit SearchTextEditor(ControlSearchPanel& ownerIn) : owner(ownerIn) {}
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGained(FocusChangeType cause) override;
        void focusLost(FocusChangeType cause) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
        void paintOverChildren(juce::Graphics& graphics) override;
    private:
        ControlSearchPanel& owner;
        bool selectOnMouseUp = false;
    };

    bool handleSearchKey(const juce::KeyPress& key);
    void chooseRow(int index);
    void selectRow(int index);
    class ResultCanvas final : public juce::Component
    {
    public:
        explicit ResultCanvas(ControlSearchPanel& ownerIn) : owner(ownerIn) {}
        void paint(juce::Graphics&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        juce::MouseCursor getMouseCursor() override;
        int getContentHeight() const noexcept;
        void resetHover()
        {
            hoveredRow = -1;
            shownHoveredRow = -1;
            ++hoverGeneration;
            repaint();
        }
    private:
        int rowAt(juce::Point<int>) const noexcept;
        ControlSearchPanel& owner;
        int hoveredRow = -1;
        int shownHoveredRow = -1;
        int hoverGeneration = 0;
    };

    SearchTextEditor input;
    UiScrollViewport listViewport;
    ResultCanvas resultsCanvas;
    std::vector<ControlSearchEntry> results;
    int selectedRow = -1;
    bool pinnedToViewport = false;
    bool resultsDismissed = false;
    juce::Rectangle<int> frame;
    void updateQuery();
    void reopenResults();
};
