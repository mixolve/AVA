#include "ChoiceControl.h"
#include "ClipboardTransfer.h"
#include "ModuleList.h"
#include "ValueEditor.h"
#include "EqlCopyEditor.h"
#include "Editor.h"
#include "EditorStateSchema.h"
#include "OscPanel.h"
#include "../routing/Panel.h"
#include "FilterSection.h"
#include "Metrics.h"
#include "PresetSection.h"
#include "../crossover/Editor.h"
#include "../crossover/ModuleBindings.h"
#include "../modules/dyn/Processor.h"
#include <array>
#include <set>
#include "../crossover/RangePage.h"
#include "../modules/shared/BlockChainProcessor.h"

void AvaAudioProcessorEditor::layoutEqlModuleSections(juce::Rectangle<int>& viewportBounds)
{
    const auto gap = getUiGap(*this);
    viewportBounds = viewportBounds.withX(0).withWidth(getWidth());
    if (controlSearchPanel != nullptr)
    {
        const auto top = getMainViewportTop(controlSearchPanel->getDisplayedHeight());
        const auto bottom = quickAccessStrip.getY();
        viewportBounds = {viewportBounds.getX(), top, viewportBounds.getWidth(),
                          juce::jmax(0, bottom - top)};
    }
    else
        viewportBounds = viewportBounds.withY(0).withHeight(getHeight());
    filterViewport.setBounds(viewportBounds);
    filterViewport.setVisible(true);

    const auto width = juce::jmax(0, viewportBounds.getWidth() - 2 * gap);
    for (auto& section : filterSections)
        if (section != nullptr)
            section->body.setVisible(section->expanded && getFilterOrderPositionForIndex(section->filterIndex) >= 0);

    std::array<std::vector<juce::Component*>, eqlScrollLabels.size()> rows;
    if (presetsSection != nullptr)
        rows[0] = {&presetsSection->presetChoice, presetsSection->addButton.get(),
            presetsSection->saveButton.get(), presetsSection->renameButton.get(),
            presetsSection->defaultButton.get(), presetsSection->deleteButton.get(),
            sortFiltersButton.get(), presetsSection->copyFiltersButton.get(),
            presetsSection->pasteFilterButton.get()};

    FilterSection* selectedFilter = nullptr;
    for (auto& section : filterSections)
        if (section != nullptr && section->expanded
            && getFilterOrderPositionForIndex(section->filterIndex) >= 0)
        {
            selectedFilter = section.get();
            break;
        }
    if (selectedFilter != nullptr)
        rows[1] = {selectedFilter->typeControl.get(), selectedFilter->orderControl.get(),
            selectedFilter->frequencyControl.get(), selectedFilter->bandwidthControl.get(),
            selectedFilter->gainControl.get()};
    const auto filterActionStart = int(rows[1].size());
    rows[1].insert(rows[1].end(), {addFilterButton.get(), deleteFilterButton.get(),
                                  copyFilterButton.get(), pasteFilterButton.get()});

    for (int position = 0; position < getActiveFilterCount(); ++position)
    {
        const auto index = getFilterIndexForOrderPosition(position);
        if (index >= 0 && filterSections[size_t(index)] != nullptr)
        {
            auto* header = filterSections[size_t(index)]->header.get();
            header->setVisible(true);
            rows[2].push_back(header);
        }
    }

    const juce::Rectangle<int> gridBounds {0, 0, width, getSquareControlSize(*this)};
    const auto moduleHeight = layoutEqlControlRows(eqlScrollRows, rows, gridBounds, filterActionStart);

    filterContent.controlBlockFrames.clear();
    filterContent.setInterceptsMouseClicks(false, true);
    filterContent.setBounds(0, 0, width, moduleHeight);
    for (auto& section : filterSections)
        if (section != nullptr)
        {
            section->body.controlBlockFrames.clear();
            section->body.setInterceptsMouseClicks(false, true);
            section->body.setBounds(0, 0, width, moduleHeight);
        }

    eqlContent.setSize(width, moduleHeight);
    filterViewport.setViewPosition(0, filterViewport.getViewPositionY());
}

int AvaAudioProcessorEditor::layoutEqlControlRows(
    const EqlScrollRows& scrollRows,
    std::array<std::vector<juce::Component*>, eqlScrollLabels.size()>& rows,
    juce::Rectangle<int> grid, int filterActionStart)
{
    if (scrollRows.back() == nullptr) return 0;

    constexpr std::array presetDivider {6};
    const std::array filterDivider {filterActionStart};
    const auto& owner = scrollRows.back()->content;
    const auto gap = getUiRowGap(owner);
    const auto tileViewportWidth = juce::jmax(0, grid.getWidth() - grid.getHeight() - getUiGap(owner));
    auto& filters = rows.back();
    const auto fittingSlots = scrollRows.back()->content.countFittingSlots(tileViewportWidth);
    filters.resize(juce::jmax(filters.size(), size_t(fittingSlots)), nullptr);

    for (size_t index = 0; index < rows.size(); ++index)
    {
        for (auto* control : rows[index])
            if (control != nullptr) control->setVisible(true);
        const auto divider = index == 0 ? std::span<const int>(presetDivider)
                           : index == 1 ? std::span<const int>(filterDivider) : std::span<const int>{};
        scrollRows[index]->layoutInline(grid, rows[index], true, {}, divider);
        grid.translate(0, grid.getHeight() + gap);
    }
    return grid.getY() - gap;
}

void AvaAudioProcessorEditor::handleEqlBackgroundClick(const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

void AvaAudioProcessorEditor::layoutUnifiedBlocks(juce::Rectangle<int>& bounds)
{
    const auto scroll = filterViewport.getViewPositionY();
    layoutEqlModuleSections(bounds);
    const auto blockWidth = filterViewport.getWidth();
    const auto instanceInset = getUiGap(*this);
    const auto innerBlockWidth = juce::jmax(0, blockWidth - 2 * instanceInset);
    auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get());
    const auto bands = crossover != nullptr ? crossover->getMainControls() : std::vector<juce::Component*>{};
    const auto sync = crossover != nullptr ? crossover->getMainSyncControls()
        : std::array<juce::Component*, 2>{};
    const auto settings = crossover != nullptr ? crossover->getInstanceMainRows() : InstanceMainRows{};

    const auto unitGap = getUiGap(*this);
    const auto squareSize = getSquareControlSize(*this);
    // UNIT is a single headerless, frameless row. Its scroll tile stays fixed
    // at the left; MAIN / OSC / SYNC controls all share one horizontal canvas.
    std::vector<juce::Component*> unitControls {
        routingControl.get(), slotsControl.get(), undoButton.get(), redoButton.get(),
        abControl.get(), abCopyButton.get(), globalBypassButton.get(),
        bypassCompensationControl.get(), unitCopyButton.get(), unitPasteButton.get(),
        unitZoomControl.get(), unitWidthControl.get(), unitDockSizeControl.get(),
        unitSampleRateLabel.get(), aboutControl.get()
    };
    std::vector<int> unitSpans(unitControls.size(), 1);
    const auto oscStart = int(unitControls.size());
    const auto oscControls = oscPanel->getControls();
    unitControls.insert(unitControls.end(), oscControls.begin(), oscControls.end());
    // HOST retains the two-column width from the former OSC row.
    unitSpans.insert(unitSpans.end(), {1, 1, 1, 1, 2});
    const auto syncStart = int(unitControls.size());
    for (auto* control : std::array<juce::Component*, 4> {
             unitNumberControl.get(), receiveControl.get(), sendControl.get(), linksCountControl.get() })
    {
        unitControls.push_back(control);
        unitSpans.push_back(1);
    }
    for (int index = 0; index < linkDestinationCount; ++index)
    {
        unitControls.push_back(linkToControls[size_t(index)].get());
        unitSpans.push_back(1);
    }
    // Two white, 2px-wide, full-control-height group separators.
    const std::array<int, 2> groupBreaks {oscStart, syncStart};
    unitMainContent.controlBlockFrames.clear();
    unitMainContent.setBounds(instanceInset, 0, innerBlockWidth, squareSize);
    unitMainRow.layoutInline({0, 0, innerBlockWidth, squareSize},
                             unitControls, true, unitSpans, groupBreaks);
    unitMainContent.setVisible(true);
    const auto instanceY = squareSize + unitGap;

    std::vector<juce::Component*> instanceControls {
        linkInstanceControl.get(), settings.secondary[0], settings.secondary[1],
        sync[0], sync[1], settings.secondary[2]
    };
    instanceControls.insert(instanceControls.end(), settings.splits.begin(), settings.splits.end());
    instanceControls.insert(instanceControls.end(), bands.begin(), bands.end());
    instanceControls.insert(instanceControls.end(), settings.actions.begin(), settings.actions.end());
    std::erase(instanceControls, nullptr);
    mainContent.setBounds(instanceInset, 0, innerBlockWidth, squareSize);
    instanceMainRow.layoutInline(mainContent.getLocalBounds(), instanceControls);

    // Set the container width before reparenting modules.
    instanceContent.setBounds(0, instanceY, blockWidth, squareSize);
    ensureModuleList();
    const auto modulesY = mainContent.getBottom() + unitGap;
    if (moduleList != nullptr)
    {
        const juce::Rectangle<int> boundsForModules {instanceInset, modulesY, innerBlockWidth,
                                                     ModuleList::getContentHeight(*this)};
        if (moduleList->getBounds() == boundsForModules) moduleList->resized();
        else moduleList->setBounds(boundsForModules);
    }
    const auto activeBand = audioProcessor.getSelectedCrossoverRange();
    const auto slots = audioProcessor.getModuleStackForRange(activeBand);
    std::array<juce::Component*, ava::module_stack::typeCount> originals {};
    std::map<int, juce::Component*> copies;
    if (auto* editor = dynamic_cast<CrossoverModuleComponent*>(blockChainModuleEditor.get()))
    {
        editor->setVisible(false);
        for (const auto& [id, component] : editor->independentBlocks(instanceContent, innerBlockWidth))
        {
            if (juce::isPositiveAndBelow(id, int(ava::modules::chain::blocks::count))) originals[size_t(module_order::fromChain(id))] = component;
            else { component->setVisible(false); component->setBounds({}); }
        }
    }
    if (auto* editor = dynamic_cast<CrossoverModuleComponent*>(dynModuleEditor.get()))
    {
        editor->getProperties().set("stackedModule", true);
        if (editor->getParentComponent() != &instanceContent) instanceContent.addChildComponent(*editor);
        editor->setBounds(0, 0, innerBlockWidth, 1);
        editor->setSize(innerBlockWidth, editor->getPreferredHeight());
        originals[size_t(module_order::dyn)] = editor;
    }
    originals[size_t(module_order::eql)] = &eqlContent;

    // Reuse CrossoverModuleComponent, including parameter binding, for every
    // copied CHAIN/DYN instance. No surrogate controls or custom rendering.
    std::set<int> requiredEditors;
    for (const auto& slot : slots)
    {
        if (slot.uid < ava::module_stack::originalCount || slot.type == module_order::eql) continue;
        requiredEditors.insert(slot.uid);
        auto found = copiedModuleEditors.find(slot.uid);
        // DYN's controls depend on both its domain and processing mode.
        // Rebuild copies by the same identity used for the original editor.
        if (found != copiedModuleEditors.end() && slot.type == module_order::dyn)
            if (auto* processor = audioProcessor.getCopiedDynProcessor(slot.uid))
                if (auto* editor = dynamic_cast<CrossoverModuleComponent*>(found->second.get());
                    editor == nullptr || editor->getProcessorIdentity() != processor->getDomainEditorIdentity(activeBand))
                {
                    copiedModuleEditors.erase(found);
                    found = copiedModuleEditors.end();
                }
        if (found == copiedModuleEditors.end())
        {
            std::unique_ptr<juce::Component> newEditor;
            if (module_order::isChain(slot.type))
            {
                if (auto* processor = audioProcessor.getCopiedBlockChainProcessor(slot.uid))
                {
                    auto config = crossover_configs::makeBlockChainCrossoverConfig(*processor);
                    config.uiState = &valueTreeState;
                    config.collapseScope = config.moduleKey + ".uid-" + juce::String(slot.uid);
                    newEditor = std::make_unique<CrossoverModuleComponent>(std::move(config));
                }
            }
            else if (auto* processor = audioProcessor.getCopiedDynProcessor(slot.uid))
            {
                auto config = crossover_configs::makeDynCrossoverConfig(*processor, *this, activeBand);
                config.uiState = &valueTreeState;
                config.collapseScope = config.moduleKey + ".uid-" + juce::String(slot.uid);
                newEditor = std::make_unique<CrossoverModuleComponent>(std::move(config));
            }
            if (!newEditor) continue;
            newEditor->getProperties().set("moduleCopyUid", slot.uid);
            newEditor->getProperties().set("moduleCopyBand", static_cast<int>(activeBand));
            addChildComponent(*newEditor);
            found = copiedModuleEditors.emplace(slot.uid, std::move(newEditor)).first;
        }
        auto* editor = dynamic_cast<CrossoverModuleComponent*>(found->second.get());
        if (editor == nullptr) continue;
        editor->setExternalCrossoverRange(activeBand);
        if (module_order::isChain(slot.type))
        {
            editor->setVisible(false);
            for (const auto& [id, component] : editor->independentBlocks(instanceContent, innerBlockWidth))
            {
                if (module_order::fromChain(id) == slot.type)
                {
                    component->getProperties().set("moduleCopyUid", slot.uid);
                    component->getProperties().set("moduleCopyBand", static_cast<int>(activeBand));
                    copies.emplace(slot.uid, component);
                }
                else { component->setVisible(false); component->setBounds({}); }
            }
        }
        else
        {
            editor->getProperties().set("stackedModule", true);
            if (editor->getParentComponent() != &instanceContent) instanceContent.addChildComponent(*editor);
            editor->setBounds(0, 0, innerBlockWidth, 1);
            editor->setSize(innerBlockWidth, editor->getPreferredHeight());
            copies.emplace(slot.uid, editor);
        }
    }
    for (auto it = copiedModuleEditors.begin(); it != copiedModuleEditors.end();)
    {
        if (!requiredEditors.contains(it->first)) it = copiedModuleEditors.erase(it);
        else ++it;
    }

    // Each copied EQL owns its own parameter-bound controls. All copies may be
    // expanded simultaneously; opening one never switches another editor.
    std::set<int> requiredEqlEditors;
    for (const auto& slot : slots)
    {
        if (slot.type != module_order::eql || slot.uid < ava::module_stack::originalCount)
            continue;
        requiredEqlEditors.insert(slot.uid);
        auto* processor = audioProcessor.getCopiedEqlProcessor(slot.uid, activeBand);
        if (processor == nullptr) continue;
        auto found = eqlCopyEditors.find(slot.uid);
        if (found != eqlCopyEditors.end()
            && (found->second->getProcessorIdentity() != processor
                || found->second->getBand() != int(activeBand)))
            found = eqlCopyEditors.erase(found);
        if (found == eqlCopyEditors.end())
        {
            auto editor = std::make_unique<EqlCopyEditor>(*this, *processor,
                slot.uid, int(activeBand));
            instanceContent.addAndMakeVisible(*editor);
            found = eqlCopyEditors.emplace(slot.uid, std::move(editor)).first;
        }
        found->second->updateLayout(innerBlockWidth);
        copies.emplace(slot.uid, found->second.get());
    }
    for (auto it = eqlCopyEditors.begin(); it != eqlCopyEditors.end();)
    {
        if (!requiredEqlEditors.contains(it->first)) it = eqlCopyEditors.erase(it);
        else ++it;
    }

    eqlContent.setVisible(false);
    if (auto* editor = originals[size_t(module_order::dyn)])
        editor->setVisible(false);

    // OFF is a true per-slot DSP bypass (see AudioProcessing.cpp). Apply the
    // same flag here so disabled copies disappear from ALL rather than leaving
    // stale EQL / CHAIN / DYN headers visible.
    std::array<int, ava::module_stack::typeCount> shownCounts {}, shownOrdinals {};
    for (const auto& slot : slots)
        if (slot.shown) ++shownCounts[size_t(slot.type)];
    int y = moduleList != nullptr ? moduleList->getBottom() : mainContent.getBottom();
    for (const auto& slot : slots)
    {
        juce::Component* block = nullptr;
        if (slot.uid < ava::module_stack::originalCount)
        {
            block = originals[size_t(slot.type)];
        }
        else if (const auto found = copies.find(slot.uid); found != copies.end())
            block = found->second;
        if (block == nullptr) continue;
        // Dock controls need the *module* instance ordinal (01, 02, ...),
        // not the routing node ID. Keep the slot identity on the owning
        // component so it is visible to nested parameter controls.
        block->getProperties().set("moduleSlotUid", slot.uid);
        block->getProperties().set("moduleSlotBand", int(activeBand));
        block->getProperties().set("moduleSlotCreated", slot.created);
        if (!slot.shown)
        {
            block->setVisible(false);
            block->setBounds({});
            continue;
        }
        const auto ownerBand = activeBand;
        const auto ownerUid = slot.uid;
        auto readBypass = [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),
                           ownerBand, ownerUid] {
            return safe == nullptr || safe->audioProcessor.isModuleSlotBypassed(ownerBand, ownerUid);
        };
        auto toggleBypass = [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),
                             ownerBand, ownerUid] {
            if (safe == nullptr) return;
            const bool bypassed = safe->audioProcessor.isModuleSlotBypassed(ownerBand, ownerUid);
            if (safe->audioProcessor.setModuleSlotEnabled(ownerBand, ownerUid, bypassed))
            {
                safe->scheduleHistorySnapshot();
                safe->resized();
            }
        };
        const auto type = size_t(slot.type);
        auto caption = juce::String(module_order::descriptions[type].name).toUpperCase();
        if (shownCounts[type] > 1)
            caption += " (" + juce::String(++shownOrdinals[type]).paddedLeft('0', 2) + ")";
        if (slot.type == module_order::eql)
        {
            if (auto* copy = dynamic_cast<EqlCopyEditor*>(block))
                copy->configureScrollTile(caption, readBypass, toggleBypass);
            else
            {
                eqlScrollRows.front()->setScrollLabel(caption + "\nSCROLL");
                eqlScrollRows.front()->setModuleBypass(readBypass, toggleBypass);
            }
        }
        else
        {
            const auto safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this);
            const auto copySettings = [safe, ownerBand, ownerUid]
            {
                if (safe != nullptr) ava::clipboard::copyModule(safe->audioProcessor, ownerBand, ownerUid);
            };
            const auto pasteSettings = [safe, ownerBand, ownerUid]
            {
                if (safe == nullptr || !ava::clipboard::pasteModule(safe->audioProcessor, ownerBand, ownerUid)) return;
                safe->scheduleModuleControlsRefresh(true);
                safe->scheduleHistorySnapshot();
                safe->audioProcessor.notifyHostOfStateChange();
            };
            if (auto* dyn = dynamic_cast<CrossoverModuleComponent*>(block))
                dyn->configureModuleScrollTile(caption, readBypass, toggleBypass, copySettings, pasteSettings);
            else
                CrossoverRangePage::configureModuleScrollTile(*block, caption, readBypass, toggleBypass,
                                                            copySettings, pasteSettings);
        }
        y += getUiGap(*this);
        block->setVisible(true);
        block->setTopLeftPosition(instanceInset, y);
        y = block->getBottom();
    }
    instanceContent.setBounds(0, instanceY, blockWidth, y);
    updateInstanceScrollLabel();
    moduleContent.setSize(blockWidth, juce::jmax(filterViewport.getHeight(), instanceY + y));
    // Preserve the user's scroll position after ADD, DEL and ON/OFF.
    // In particular, never jump to a newly created module automatically.
    filterViewport.setViewPosition(0, scroll);
    updatePinnedSearchPosition();
    if (routingInstanceId != 0)
        if (auto* root = audioProcessor.getRoutingOwner().getContentEditor())
            if (root != this) root->updatePinnedSearchPosition();
    refreshQuickAccessControls();
}

void AvaAudioProcessorEditor::updatePinnedSearchPosition()
{
    if (controlSearchPanel == nullptr || updatingPinnedSearchPosition) return;
    const juce::ScopedValueSetter<bool> guard(updatingPinnedSearchPosition, true);
    const auto* visibleEditor = activeInstanceEditor != nullptr ? activeInstanceEditor.get() : this;
    const bool showingEditor = !isDockOnly() && !visibleEditor->hasUnitPage()
        && filterViewport.isVisible();
    const bool showSearch = showingEditor && activeInstanceEditor == nullptr;
    controlSearchPanel->setVisible(showSearch);
    const auto searchHeight = visibleEditor->controlSearchPanel != nullptr
        ? visibleEditor->controlSearchPanel->getDisplayedHeight()
        : controlSearchPanel->getDisplayedHeight();

    // Keep the permanent pinned INSTANCE strip on the root editor. Nested
    // editors reserve its height without painting a second strip.
    const bool showRootRouting = showingEditor && routingInstanceId == 0;
    if (!showRootRouting)
    {
        pinnedRoutingViewport.setVisible(false);
        if (pinnedRoutingScrollControl != nullptr) pinnedRoutingScrollControl->setVisible(false);
    }
    if (pinnedVerticalScrollControl != nullptr)
    {
        pinnedVerticalScrollControl->setVisible(showRootRouting);
        if (!showRootRouting) pinnedVerticalScrollControl->setBounds({});
    }
    if (!showingEditor) return;

    const auto gap = getUiGap(*this);
    const auto searchTop = 2 * gap;
    const auto width = visibleEditor->getWidth();
    const auto x = gap;
    const auto unitWidth = juce::jmax(0, width - 2 * gap);
    const auto routingTop = searchTop + searchHeight + gap;
    const auto tileHeight = getSquareControlSize(*this);
    const auto scrollStep = tileHeight + gap;
    // The rightmost pinned tile is reserved for vertical page scrolling.
    if (showRootRouting && pinnedVerticalScrollControl != nullptr)
    {
        pinnedVerticalScrollControl->setBounds(
            x + unitWidth - tileHeight, routingTop, tileHeight, tileHeight);
        syncVerticalScrollControl();
    }
    if (showSearch)
        controlSearchPanel->setBounds(x, searchTop, unitWidth, searchHeight);
    if (showRootRouting)
    {
        const auto scrollX = x;
        const auto availableWidth = juce::jmax(0, unitWidth - scrollStep);
        pinnedRoutingScrollControl->setBounds(scrollX, routingTop, tileHeight, tileHeight);
        pinnedRoutingScrollControl->setVisible(true);
        pinnedRoutingViewport.setBounds(scrollX + scrollStep, routingTop,
            juce::jmax(0, availableWidth - scrollStep), tileHeight);
        pinnedRoutingViewport.setVisible(true);
        pinnedRoutingContent.layoutTrailingSlots(int(pinnedRoutingButtons.size()), pinnedRoutingViewport.getWidth());
        if (pinnedRoutingContent.getWidth() <= pinnedRoutingViewport.getWidth())
            pinnedRoutingViewport.setViewPosition(0, 0);
        syncPinnedRoutingScrollControl();
    }
    else
    {
        pinnedRoutingViewport.setBounds({});
        pinnedRoutingScrollControl->setBounds({});
        pinnedRoutingScrollControl->setVisible(false);
    }
    raisePinnedControls();
    if (activeInstanceEditor != nullptr) return;
    const auto top = getMainViewportTop(searchHeight);
    const auto bottom = quickAccessStrip.getY();
    const juce::Rectangle<int> visibleBounds{0, top, width, juce::jmax(0, bottom - top)};
    if (filterViewport.getBounds() != visibleBounds)
        filterViewport.setBounds(visibleBounds);
    if (routingInstanceId == 0) syncVerticalScrollControl();
}
