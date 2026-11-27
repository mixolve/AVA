#include "Editor.h"
#include "ChoiceControl.h"
#include "FilterSection.h"
#include "OscPanel.h"

#include <algorithm>
#include <functional>

#include "UppercaseText.h"
#include <utility>

std::vector<ControlSearchEntry> AvaAudioProcessorEditor::findControls(const juce::String& query)
{
    const auto needle = normaliseControlSearchKey(query);
    const auto swapped = normaliseControlSearchKey(swapKeyboardLayout(query));
    std::vector<ControlSearchEntry> matches;
    if (needle.isEmpty()) return matches;

    const auto append = [&matches, &needle, &swapped](const juce::String& caption, const juce::String& location,
                                            juce::Component& component, const juce::String& blockTitle = {})
    {
        if (caption.isEmpty()) return;
        const auto displayedCaption = visibleControlCaption(caption).replaceCharacter('\n', ' ');
        const auto captionKey = normaliseControlSearchKey(displayedCaption);
        // Match the visible spelling and the same physical keys typed with
        // the other keyboard layout (both Cyrillic -> Latin and vice versa).
        if (!captionKey.contains(needle) && !captionKey.contains(swapped)) return;
        matches.push_back({ displayedCaption, visibleControlCaption(location).replaceCharacter('\n', ' '),
                            juce::Component::SafePointer<juce::Component>(&component), blockTitle });
    };
    std::function<void(juce::Component&, juce::StringArray)> visit;
    visit = [&](juce::Component& component, juce::StringArray ancestors)
    {
        if (&component == controlSearchPanel.get()) return;
        // An inactive BAND's stale block rows can still be children of ALL with
        // zero/invisible bounds. They must not pollute the current band's search.
        if (component.getParentComponent() == &instanceContent && !component.isVisible()
            && !instanceContent.isBlockCollapsed("INSTANCE")) return;
        // A disabled module has no target in the ALL viewport. Collapsed blocks,
        // by contrast, are indexed with their children so a result can reopen them.
        if (&component == &eqlContent
            && audioProcessor.isModuleHiddenForRange(audioProcessor.getSelectedCrossoverRange(),
                                                      module_order::eql)) return;
        if (&component == dynModuleEditor.get() && !component.isVisible()) return;
        // Filter objects are allocated at maximum capacity but only the active
        // ones have a position in the EQL page.
        for (size_t index = static_cast<size_t>(getActiveFilterCount());
             index < filterSections.size(); ++index)
            if (auto* section = filterSections[index].get(); section != nullptr
                && (&component == &section->body || &component == section->header.get()))
                return;

        if (auto* block = dynamic_cast<ControlBlockComponent*>(&component))
        {
            for (const auto& frame : block->controlBlockFrames)
            {
                if (frame.title.isEmpty()) continue;
                auto path = ancestors;
                path.add(frame.displayTitle.isNotEmpty() ? frame.displayTitle : frame.title);
                append(frame.displayTitle.isNotEmpty() ? frame.displayTitle : frame.title, path.joinIntoString(" / "), component, frame.title);
            }
            if (block->controlBlockFrames.size() == 1)
            {
                const auto title = block->controlBlockFrames.front().title;
                if (title.isNotEmpty()) ancestors.add(visibleControlCaption(block->controlBlockFrames.front().displayTitle.isNotEmpty()
                    ? block->controlBlockFrames.front().displayTitle : title));
            }
        }
        if (auto* control = dynamic_cast<ParameterField*>(&component))
        {
            const auto title = visibleControlCaption(control->getTitleText()).replaceCharacter('\n', ' ');
            auto path = ancestors;
            path.add(title);
            append(title, path.joinIntoString(" / "), component);
            return;
        }
        if (auto* button = dynamic_cast<BoxTextButton*>(&component))
        {
            const auto title = visibleControlCaption(button->getButtonText()).replaceCharacter('\n', ' ');
            auto path = ancestors;
            path.add(title);
            append(title, path.joinIntoString(" / "), component);
            return;
        }
        if (auto* label = dynamic_cast<SquareTextLabel*>(&component))
        {
            const auto title = component.getName() == "status" ? juce::String("FREE/BUSY") : visibleControlCaption(label->getText()).replaceCharacter('\n', ' ');
            auto path = ancestors;
            path.add(title);
            append(title, path.joinIntoString(" / "), component);
            return;
        }
        for (int childIndex = 0; childIndex < component.getNumChildComponents(); ++childIndex)
            if (auto* child = component.getChildComponent(childIndex)) visit(*child, ancestors);
    };

    visit(moduleContent, {});
    for (auto* child : slotsContent.getChildren())
        visit(*child, {"UNIT", "SLOTS"});
    std::stable_sort(matches.begin(), matches.end(), [](const auto& left, const auto& right)
    {
        return left.location.compareIgnoreCase(right.location) < 0;
    });

    // A time control contains two alternative presentations (milliseconds and
    // note-sync). Only one is visible at a time, but both live in the component
    // tree. Keep a single search result per path and favour its visible target.
    std::vector<ControlSearchEntry> distinct;
    distinct.reserve(matches.size());
    for (auto& match : matches)
    {
        if (!distinct.empty() && distinct.back().location.compareIgnoreCase(match.location) == 0)
        {
            const auto* previous = distinct.back().target.getComponent();
            const auto* candidate = match.target.getComponent();
            if (candidate != nullptr && candidate->isVisible()
                && (previous == nullptr || !previous->isVisible()))
                distinct.back() = std::move(match);
        }
        else
        {
            distinct.push_back(std::move(match));
        }
    }
    const auto commandMatches = [&needle, &swapped](const juce::StringArray& aliases)
    {
        for (const auto& alias : aliases)
        {
            const auto key = normaliseControlSearchKey(alias);
            if (key.startsWith(needle) || key.startsWith(swapped)) return true;
        }
        return false;
    };
    if (commandMatches({"COLLAPSE", "COLLAPSE ALL", "FOLD", "CLOSE ALL"}))
        distinct.insert(distinct.begin(), {"COLLAPSE", "ACTION / COLLAPSE ALL BLOCKS", {}, {},
            ControlSearchEntry::Action::collapseAll});
    if (commandMatches({"UP", "TOP"}))
        distinct.insert(distinct.begin(), {"UP", "ACTION / SCROLL TO TOP", {}, {},
            ControlSearchEntry::Action::scrollTop});
    return distinct;
}

void AvaAudioProcessorEditor::navigateToSearchResult(ControlSearchEntry result)
{
    if (result.action == ControlSearchEntry::Action::scrollTop)
    {
        filterViewport.setViewPosition(0, 0);
        storeEditorUiState();
        updatePinnedSearchPosition();
        return;
    }
    if (result.action == ControlSearchEntry::Action::collapseAll)
    {
        // Visit children even when their parent is currently folded, so
        // reopening UNIT or INSTANCE does not expose uncollapsed descendants.
        const auto fold = [&](const auto& self, juce::Component& component) -> void
        {
            for (auto* child : component.getChildren()) self(self, *child);
            if (auto* block = dynamic_cast<ControlBlockComponent*>(&component))
                block->collapseAllBlocks();
        };
        fold(fold, moduleContent);
        resized();
        filterViewport.setViewPosition(0, 0);
        storeEditorUiState();
        updatePinnedSearchPosition();
        return;
    }
    auto* target = result.target.getComponent();
    if (target == nullptr) return;

    if (auto* previous = dynamic_cast<ControlBlockComponent*>(searchFlashTarget.getComponent()))
        previous->setSearchHighlightedTitle({});
    searchFlashTarget = nullptr;
    searchFlashBlockTitle.clear();

    if (slotsContent.isParentOf(target))
    {
        showUnitPage(UnitPage::slots);
        const auto rect = slotsContent.getLocalArea(target, target->getLocalBounds());
        slotsViewport.setViewPosition(0, rect.getCentreY() - slotsViewport.getHeight() / 2);
        searchFlashTarget = target;
        searchFlashStarted = juce::Time::getMillisecondCounter();
        searchFlashOn = true;
        repaint();
        return;
    }
    if (hasUnitPage()) showUnitPage(UnitPage::main);

    // Expand only the ancestors of a result, not a clicked block caption itself.
    bool changed = false;
    for (auto* parent = result.blockTitle.isNotEmpty() ? target->getParentComponent() : target;
         parent != nullptr; parent = parent->getParentComponent())
        if (auto* block = dynamic_cast<ControlBlockComponent*>(parent))
            changed = block->expandAllBlocks() || changed;

    // EQL filter parameters can additionally be hidden behind the selection
    // of a different filter, independently of the block collapse state.
    for (int i = 0; i < getActiveFilterCount(); ++i)
            if (auto* section = filterSections[static_cast<size_t>(i)].get(); section != nullptr
                && (&section->body == target || section->body.isParentOf(target))
                && !section->expanded)
            {
                enforceSingleExpandedFilterSection(i);
                changed = true;
                break;
            }
    if (changed) updateSectionStates();
    resized();

    auto& content = static_cast<juce::Component&>(moduleContent);
    auto& viewport = static_cast<juce::Viewport&>(filterViewport);
    juce::Rectangle<int> targetRect = target->getLocalBounds();
    if (result.blockTitle.isNotEmpty())
        if (auto* block = dynamic_cast<ControlBlockComponent*>(target))
            for (const auto& frame : block->controlBlockFrames)
                if (frame.title == result.blockTitle)
                {
                    targetRect = frame.bounds.withHeight(controlBlockHeaderHeight);
                    break;
                }
    if (targetRect.isEmpty()) return;
    if (auto* row = target->findParentComponentOfClass<HorizontalControlViewport>())
        if (auto* rowContent = row->getViewedComponent())
        {
            const auto item = rowContent->getLocalArea(target, targetRect);
            const auto visible = row->getViewArea();
            if (item.getX() < visible.getX()) row->setViewPosition(item.getX(), 0);
            else if (item.getRight() > visible.getRight())
                row->setViewPosition(item.getRight() - visible.getWidth(), 0);
        }

    const auto rect = content.getLocalArea(target, targetRect);
    const auto viewportHeight = viewport.getHeight();
    const auto centered = rect.getCentreY() - viewportHeight / 2;
    const auto maximum = juce::jmax(0, content.getHeight() - viewportHeight);
    viewport.setViewPosition(0, juce::jlimit(0, maximum, centered));
    storeEditorUiState();

    searchFlashTarget = target;
    searchFlashBlockTitle = result.blockTitle;
    searchFlashStarted = juce::Time::getMillisecondCounter();
    searchFlashOn = true;
    if (auto* block = dynamic_cast<ControlBlockComponent*>(target))
        if (searchFlashBlockTitle.isNotEmpty())
            block->setSearchHighlightedTitle(searchFlashBlockTitle);
    repaint();
}

void AvaAudioProcessorEditor::updateSearchFlash()
{
    if (searchFlashTarget == nullptr || searchFlashStarted == 0) return;
    const auto elapsed = juce::Time::getMillisecondCounter() - searchFlashStarted;
    const auto active = elapsed < 1100u;
    const auto illuminated = active && (elapsed / 140u) % 2u == 0u;
    if (illuminated != searchFlashOn || !active)
    {
        searchFlashOn = illuminated;
        if (auto* block = dynamic_cast<ControlBlockComponent*>(searchFlashTarget.getComponent()))
            if (searchFlashBlockTitle.isNotEmpty())
                block->setSearchHighlightedTitle(illuminated ? searchFlashBlockTitle : juce::String{});
        repaint();
    }
    if (!active)
    {
        searchFlashTarget = nullptr;
        searchFlashBlockTitle.clear();
        searchFlashStarted = 0;
    }
}
