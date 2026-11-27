#include "EqlCopyEditor.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "Style.h"
#include "Prompt.h"
#include "ModuleList.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../crossover/Editor.h"
#include "../crossover/ParameterIds.h"

#include <array>
#include <algorithm>
#include <utility>

void AvaAudioProcessorEditor::showTextPrompt(const juce::String& currentText,
                                             std::function<bool(const juce::String&)> onCommit,
                                             juce::Rectangle<int> anchorBounds,
                                             std::function<void()> onClose,
                                             std::function<void()> onDismiss,
                                             const bool uppercaseInput,
                                             juce::Slider* numericControl,
                                             std::function<double(const juce::String&)> textToValue,
                                             std::function<juce::String()> valueToText,
                                             std::function<void()> resetAction,
                                             juce::Component* anchorComponent)
{
    prepareChildPrompt();

    const auto preservedFilterScrollY = filterViewport.getViewPositionY();

    auto prompt = makeTextPrompt(currentText,
                                 std::move(onCommit),
                                 anchorBounds,
                                 std::move(onDismiss),
                                 makeScrollPreservingPromptClose(*this, std::move(onClose)),
                                 uppercaseInput, false, numericControl, std::move(textToValue), std::move(valueToText));
    prompt->setResetAction(std::move(resetAction));
    if (!suspendedPromptOverlays.empty())
        for (const auto* property : { "hidesModuleContent", "paintControlIndicators" })
            prompt->getProperties().set(property,
                suspendedPromptOverlays.back()->getProperties().getWithDefault(property, false));
    auto* promptComponent = prompt.get();
    textPromptOverlay = std::move(prompt);
    addAndMakeVisible(*textPromptOverlay);
    const bool anchoredNumericEdit = anchorComponent != nullptr && numericControl != nullptr
        && !bool(textPromptOverlay->getProperties().getWithDefault("hidesModuleContent", false));
    if (anchoredNumericEdit)
    {
        // The numeric prompt overlays an existing control; opening it must not
        // reflow MODULES and reparent its live parameter-bound rows mid-click.
        textPromptOverlay->setBounds(getLocalBounds());
        promptComponent->followAnchor(*anchorComponent);
    }
    else
    {
        if (anchorComponent != nullptr) promptComponent->followAnchor(*anchorComponent);
        resized();
    }

    // Without a reflow the viewport is already at its original position.
    if (!anchoredNumericEdit && filterViewport.isVisible())
    {
        const auto maxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
        filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, preservedFilterScrollY));
    }

    juce::MessageManager::callAsync([safePrompt = juce::Component::SafePointer<PromptComponent>(promptComponent)]
                                    {
                                        if (safePrompt != nullptr)
                                        {
                                            safePrompt->toFront(false);
                                            safePrompt->focusEditor();
                                        }
                                    });

    juce::Timer::callAfterDelay(25, [safePrompt = juce::Component::SafePointer<PromptComponent>(promptComponent)]
                                    {
                                        if (safePrompt != nullptr)
                                            safePrompt->focusEditor();
                                    });
}

void AvaAudioProcessorEditor::showChoicePrompt(const juce::Rectangle<int>& anchorBounds,
                                               const juce::StringArray& choices,
                                               int selectedIndex,
                                               std::vector<bool> itemEnabledStates,
                                               const juce::Justification itemJustification,
                                               std::function<void(int)> onSelect,
                                               std::function<void()> onClose,
                                               std::function<void()> onDismiss,
                                               const bool commitBeforeFade,
                                               std::vector<bool> itemActiveStates)
{
    const auto preservedFilterScrollY = filterViewport.getViewPositionY();
    prepareChildPrompt();

    auto prompt = makeChoicePrompt(anchorBounds,
                                   choices,
                                   selectedIndex,
                                   std::move(itemEnabledStates),
                                   itemJustification,
                                   std::move(onSelect),
                                   std::move(onDismiss),
                                   makeScrollPreservingPromptClose(*this, std::move(onClose)),
                                   commitBeforeFade, std::move(itemActiveStates));
    if (!suspendedPromptOverlays.empty())
        for (const auto* property : { "hidesModuleContent", "paintControlIndicators" })
            prompt->getProperties().set(property,
                suspendedPromptOverlays.back()->getProperties().getWithDefault(property, false));
    auto* promptComponent = prompt.get();
    textPromptOverlay = std::move(prompt);
    addAndMakeVisible(*textPromptOverlay);
    resized();

    if (filterViewport.isVisible())
    {
        const auto maxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
        filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, preservedFilterScrollY));
    }

    promptComponent->toFront(true);
}

void AvaAudioProcessorEditor::prepareChildPrompt()
{
    if (textPromptOverlay != nullptr
        && bool(textPromptOverlay->getProperties().getWithDefault("allowsChildPrompts", false)))
        suspendedPromptOverlays.push_back(std::move(textPromptOverlay));
    else
        dismissTextPrompt();
}

void AvaAudioProcessorEditor::closeTextPrompt()
{
    if (textPromptOverlay == nullptr) return;
    const auto restoreModuleContent = bool(textPromptOverlay->getProperties().getWithDefault("hidesModuleContent", false));
    removeChildComponent(textPromptOverlay.get());
    textPromptOverlay.reset();
    if (!suspendedPromptOverlays.empty())
    {
        textPromptOverlay = std::move(suspendedPromptOverlays.back());
        suspendedPromptOverlays.pop_back();
    }
    if (restoreModuleContent) resized();
    clearKeyboardFocus(*this);
    if (auto* prompt = dynamic_cast<PromptComponent*>(textPromptOverlay.get())) prompt->focusEditor();
    repaint();
}

void AvaAudioProcessorEditor::dismissTextPrompt()
{
    while (textPromptOverlay != nullptr) closeTextPrompt();
}

std::function<void()> AvaAudioProcessorEditor::makeScrollPreservingPromptClose(AvaAudioProcessorEditor& editor,
                                                                               std::function<void()> closeHandler)
{
    return [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(&editor),
            closeCallback = std::move(closeHandler)]
    {
        if (safeEditor == nullptr)
            return;

        const auto closePreservedFilterScrollY = safeEditor->filterViewport.getViewPositionY();
        safeEditor->closeTextPrompt();

        if (safeEditor->filterViewport.isVisible())
        {
            const auto maxOffset = juce::jmax(0, safeEditor->getActiveFilterContentHeight() - safeEditor->filterViewport.getHeight());
            safeEditor->filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, closePreservedFilterScrollY));
        }

        if (closeCallback)
            closeCallback();
    };
}

void AvaAudioProcessorEditor::ensureModuleList()
{
    const auto band = int(audioProcessor.getSelectedCrossoverRange());
    if (moduleList != nullptr && moduleListBand == band) return;
    if (moduleList != nullptr)
    {
        moduleList.reset();
    }
    moduleListBand = band;
    const auto range = audioProcessor.getSelectedCrossoverRange();
    auto safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this);
    ModuleList::Actions actions;
    actions.readChain = [this, range]
    {
        std::vector<ModuleListItem> items;
        const auto stack = audioProcessor.getModuleStackForRange(range);
        std::array<int, ava::module_stack::typeCount> counts {}, ordinal {};
        for (const auto& slot : stack)
            if (slot.shown) ++counts[size_t(slot.type)];
        for (const auto& slot : stack)
            if (slot.shown)
            {
                const auto type = size_t(slot.type);
                items.push_back({slot.uid, module_order::descriptions[type].name,
                                 counts[type] > 1 ? ++ordinal[type] : 0});
            }
        return items;
    };
    actions.reorder = [safe, range](const ModuleList::Order& active)
    {
        if (safe == nullptr) return;
        const auto stack = safe->audioProcessor.getModuleStackForRange(range);
        if (size_t(std::count_if(stack.begin(), stack.end(), [](const auto& slot) { return slot.shown; })) != active.size())
            return;
        std::vector<int> order;
        auto next = active.begin();
        for (const auto& slot : stack)
            order.push_back(slot.shown ? *next++ : slot.uid);
        if (!safe->audioProcessor.moveModuleSlots(range, order)) return;
        safe->resized();
        safe->scheduleHistorySnapshot();
    };
    actions.insert = [safe, range](int type, size_t index)
    {
        if (safe == nullptr || safe->audioProcessor.insertModuleAt(range, type, index) == -1) return;
        safe->rebindActiveModuleEditors();
        safe->updateSectionStates();
        safe->resized();
        if (safe->controlSearchPanel != nullptr) safe->controlSearchPanel->refreshMatches();
        safe->scheduleHistorySnapshot();
    };
    actions.remove = [safe, range](int uid, bool permanently)
    {
        if (safe == nullptr) return;
        const auto stack = safe->audioProcessor.getModuleStackForRange(range);
        const auto* slot = ava::module_stack::find(stack, uid);
        if (slot == nullptr || !slot->shown) return;
        const auto copies = std::count_if(stack.begin(), stack.end(), [type = slot->type](const auto& entry)
        {
            return entry.type == type;
        });
        if (permanently || (uid >= ava::module_stack::originalCount && copies > 1))
        {
            safe->copiedModuleEditors.erase(uid);
            safe->eqlCopyEditors.erase(uid);
            if (!safe->audioProcessor.deleteModuleSlot(range, uid)) return;
            safe->rebindActiveModuleEditors();
        }
        else if (!safe->audioProcessor.setModuleSlotShown(range, uid, false)) return;
        safe->resized();
        if (safe->controlSearchPanel != nullptr) safe->controlSearchPanel->refreshMatches();
        safe->scheduleHistorySnapshot();
    };
    moduleList = std::make_unique<ModuleList>(std::move(actions), valueTreeState);
    instanceContent.addAndMakeVisible(*moduleList);
}
