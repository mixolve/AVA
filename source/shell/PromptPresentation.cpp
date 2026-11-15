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
    if (anchorComponent != nullptr) promptComponent->followAnchor(*anchorComponent);
    resized();

    if (filterViewport.isVisible())
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

void AvaAudioProcessorEditor::toggleAboutSection()
{
    aboutExpanded = true;
    if (aboutExpanded)
    {
        dismissTextPrompt();
    }
    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
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
        modulesContent.removeChildComponent(moduleList.get());
        moduleList.reset();
    }
    moduleListBand = band;
    const auto range = audioProcessor.getSelectedCrossoverRange();
    const auto buildItems = [this,range]() -> std::vector<ModuleListItem>
    {
        std::vector<ModuleListItem> items;
        auto* processor = audioProcessor.getBlockChainProcessor();
        const auto slots = audioProcessor.getModuleStackForRange(range);
        std::array<int, ava::module_stack::typeCount> instancesByType {};
        for (const auto& slot : slots)
            if (juce::isPositiveAndBelow(slot.type, int(instancesByType.size())))
                ++instancesByType[size_t(slot.type)];
        for (const auto& slot : slots)
        {
            ModuleListItem item;
            item.id=slot.uid;
            item.name=module_order::descriptions[size_t(slot.type)].name;
            item.serial=ava::module_stack::displayOrdinal(slots, slot.uid);
            // The original modules also have per-block hidden parameters. Display
            // their EFFECTIVE state, not the separate cached slot flag.
            item.enabled=slot.enabled;
            if (slot.uid < ava::module_stack::originalCount)
            {
                if (module_order::isChain(slot.type) && processor != nullptr)
                {
                    const auto parameterId=ava::crossover::parameters::makeRangeParameterId(range,
                        ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(slot.type))].hiddenSuffix);
                    if (auto* hidden=processor->getValueTreeState().getParameter(parameterId))
                        item.enabled = item.enabled && hidden->getValue() < 0.5f;
                }
                else if (slot.type == module_order::eql || slot.type == module_order::dyn)
                    item.enabled = item.enabled && !audioProcessor.isModuleHiddenForRange(range, slot.type);
            }
            item.canDelete = instancesByType[size_t(slot.type)] > 1;
            items.push_back(std::move(item));
        }
        return items;
    };
    auto items=buildItems();
    ModuleList::Order order;
    for(const auto& item:items)order.push_back(item.id);
    auto moduleListView = std::make_unique<ModuleList>(std::move(order),
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),range](const ModuleList::Order& next)
        {
            if (safe == nullptr) return;
            if (!safe->audioProcessor.moveModuleSlots(range,next))return;
            safe->resized();
            safe->scheduleHistorySnapshot();
        }, std::move(items));
    moduleListView->setInstanceActions(
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),range](int id) -> int
        {
            if (safe == nullptr) return -1;
            const int createdUid = safe->audioProcessor.addModuleCopy(range, id);
            if (createdUid >= 0) {
                // MODULES and ALL both reflect the processor stack, but ALL
                // owns separate parameter-attached editors for its copies.
                // Refresh these bindings before laying out the added instance.
                // ADD must not change the ALL viewport's scroll position.
                // The new slot appears in MODULES and ALL at its stack position.
                safe->rebindActiveModuleEditors();
                safe->updateSectionStates();
                safe->resized();
                if (safe->controlSearchPanel != nullptr)
                    safe->controlSearchPanel->refreshMatches();
                safe->scheduleHistorySnapshot();
            }
            return createdUid;
        },
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),range](int id) -> bool
        {
            if (safe == nullptr) return false;
            // Remove parameter-attached copy editors while their processors
            // are still alive. Deleting the processor first can leave JUCE
            // attachments referring to freed parameters until resized().
            const auto oldSlots = safe->audioProcessor.getModuleStackForRange(range);
            const auto* removed = ava::module_stack::find(oldSlots, id);
            if (removed == nullptr) return false;
            const auto type = removed->type;
            if (std::count_if(oldSlots.begin(), oldSlots.end(), [type](const auto& slot)
                { return slot.type == type; }) <= 1) return false;
            safe->copiedModuleEditors.erase(id);
            safe->eqlCopyEditors.erase(id);
            if (!safe->audioProcessor.deleteModuleSlot(range, id)) return false;
            safe->scheduleHistorySnapshot();
            safe->rebindActiveModuleEditors();
            safe->resized();
            return true;
        },
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),range](int id)
        {
            if(safe==nullptr)return;
            const auto slots=safe->audioProcessor.getModuleStackForRange(range);
            const auto* slot=ava::module_stack::find(slots,id);
            if(slot==nullptr)return;
            bool effectiveEnabled=slot->enabled;
            if (id < ava::module_stack::originalCount)
            {
                if (module_order::isChain(slot->type))
                {
                    if (auto* chain=safe->audioProcessor.getBlockChainProcessor())
                    {
                        const auto parameterId=ava::crossover::parameters::makeRangeParameterId(range,
                            ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(slot->type))].hiddenSuffix);
                        if (auto* hidden=chain->getValueTreeState().getParameter(parameterId))
                            effectiveEnabled=effectiveEnabled && hidden->getValue() < 0.5f;
                    }
                }
                else effectiveEnabled=effectiveEnabled
                    && !safe->audioProcessor.isModuleHiddenForRange(range,slot->type);
            }
            if(safe->audioProcessor.setModuleSlotEnabled(range,id,!effectiveEnabled))
                safe->scheduleHistorySnapshot();
        },
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)](int id)
        {
            if(safe==nullptr)return;
            safe->audioProcessor.selectModuleCopy(id);
            safe->rebindActiveModuleEditors();
            safe->resized();
        },
        buildItems);
    moduleListView->setLayoutChanged(
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
        {
            if (safe != nullptr) safe->resized();
        });
    moduleList = std::move(moduleListView);
    modulesContent.addAndMakeVisible(*moduleList);
}
