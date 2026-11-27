#include "ChoiceControl.h"
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
#include "../crossover/Page.h"
#include "../modules/shared/BlockChainProcessor.h"

void AvaAudioProcessorEditor::layoutEqlModuleSections(juce::Rectangle<int>& viewportBounds)
{
    const auto bandScope = "eql.band-" + juce::String(int(audioProcessor.getSelectedCrossoverRange() + 1));
    // The original EQL has its own controls; copied EQL editors have separate
    // controls and persistent per-UID collapse keys.
    eqlContent.bindCollapseState(valueTreeState,
        bandScope + ".module.uid-" + juce::String(module_order::eql));
    filterContent.bindCollapseState(valueTreeState, bandScope + ".filters");
    for (size_t index = 0; index < filterSections.size(); ++index)
        if (filterSections[index] != nullptr)
            filterSections[index]->body.bindCollapseState(valueTreeState,
                bandScope + ".filter-" + juce::String(int(index + 1)));

    const auto gap = getUiGap(*this);
    if (controlSearchPanel != nullptr)
    {
        controlSearchPanel->setPinned(filterViewport.getViewPositionY() > 0);
        const auto searchTop = 2 * gap;
        const bool searchEnabled = shell_editor_ui::isSearchVisible(getRootWindowState());
        const auto searchHeight = searchEnabled
            ? controlSearchPanel->getDisplayedHeight() + gap : 0;
        const auto pinnedHeight = bool(getRootWindowState().getProperty(shell_editor_ui::key("pin_routing"), true))
            ? getSquareControlSize(*this) + gap : 0;
        const auto top = searchTop + searchHeight + pinnedHeight;
        const auto bottom = quickAccessStrip.getY();
        viewportBounds = {viewportBounds.getX(), top, viewportBounds.getWidth(),
                          juce::jmax(0, bottom - top)};
    }
    else
        viewportBounds = viewportBounds.withY(0).withHeight(getHeight());
    filterViewport.setBounds(viewportBounds);
    filterViewport.setVisible(true);

    // EQL is nested inside the larger white INSTANCE frame. Keep its
    // six-column geometry and leave a standard gap on each side.
    const auto nestedWidth = juce::jmax(0, viewportBounds.getWidth()
        - 2 * (gap + controlBlockFrameThickness));
    const auto frameWidth = juce::jmax(0, nestedWidth - gap);
    const auto inset = controlBlockPadding + controlBlockFrameThickness;
    const auto innerWidth = juce::jmax(1, frameWidth - 2 * inset);
    const auto controlsTop = controlBlockHeaderHeight + gap;
    eqlContent.controlBlockFrames.clear();
    const auto collapsed = eqlContent.isBlockCollapsed("EQL");
    filterContent.setVisible(!collapsed);
    for (auto& section : filterSections)
        if (section != nullptr)
            section->body.setVisible(!collapsed && section->expanded
                && getFilterOrderPositionForIndex(section->filterIndex) >= 0);

    if (collapsed)
    {
        if (presetsSection != nullptr)
            for (auto* control : std::array<juce::Component*, 8> {
                    &presetsSection->presetChoice, presetsSection->addButton.get(),
                    presetsSection->saveButton.get(), presetsSection->renameButton.get(),
                    presetsSection->defaultButton.get(), presetsSection->deleteButton.get(),
                    presetsSection->copyFiltersButton.get(), presetsSection->pasteFilterButton.get() })
                if (control != nullptr) { control->setVisible(false); control->setBounds({}); }
        const auto height = controlBlockHeaderHeight;
        eqlContent.controlBlockFrames.push_back({{0, 0, frameWidth, height}, "EQL", uiGreyDark});
        eqlContent.setSize(nestedWidth, height);
        return;
    }

    std::array<std::vector<ControlGridCell>, 4> rows;
    if (presetsSection != nullptr)
    {
        for (auto* control : std::array<juce::Component*, 8> {
                    &presetsSection->presetChoice, presetsSection->addButton.get(),
                    presetsSection->saveButton.get(), presetsSection->renameButton.get(),
                    presetsSection->defaultButton.get(), presetsSection->deleteButton.get(),
                    presetsSection->copyFiltersButton.get(), presetsSection->pasteFilterButton.get() })
            if (control != nullptr) control->setVisible(true);
        rows[0] = {
            {&presetsSection->presetChoice}, {presetsSection->addButton.get()},
            {presetsSection->saveButton.get()}, {presetsSection->renameButton.get()},
            {presetsSection->defaultButton.get()}, {presetsSection->deleteButton.get()}};
        rows[1] = {{presetsSection->copyFiltersButton.get()},
                   {presetsSection->pasteFilterButton.get()}};
    }
    if (sortFiltersButton != nullptr)
    {
        sortFiltersButton->setVisible(true);
        rows[1].push_back({sortFiltersButton.get()});
    }

    FilterSection* selectedFilter = nullptr;
    for (auto& section : filterSections)
        if (section != nullptr && section->expanded
            && getFilterOrderPositionForIndex(section->filterIndex) >= 0)
        {
            selectedFilter = section.get();
            break;
        }
    if (selectedFilter != nullptr)
    {
        rows[2] = {
            {selectedFilter->typeControl.get()}, {selectedFilter->orderControl.get()},
            {selectedFilter->frequencyControl.get()}, {selectedFilter->bandwidthControl.get()},
            {selectedFilter->gainControl.get()}};
        for (const auto& cell : rows[2])
            if (cell.component != nullptr) cell.component->setVisible(true);

        selectedFilter->body.addAndMakeVisible(*copyFilterButton);
        selectedFilter->body.addAndMakeVisible(*pasteFilterButton);
        selectedFilter->body.addAndMakeVisible(*deleteFilterButton);
        rows[3] = {{copyFilterButton.get()}, {pasteFilterButton.get()},
                   {deleteFilterButton.get()}};
    }
    else
    {
        if (copyFilterButton != nullptr) copyFilterButton->setVisible(false);
        if (pasteFilterButton != nullptr) pasteFilterButton->setVisible(false);
        if (deleteFilterButton != nullptr) deleteFilterButton->setVisible(false);
    }

    if (addFilterButton != nullptr)
    {
        addFilterButton->setVisible(true);
        rows[3].push_back({addFilterButton.get()});
    }
    for (int position = 0; position < getActiveFilterCount(); ++position)
    {
        const auto index = getFilterIndexForOrderPosition(position);
        if (index >= 0 && filterSections[size_t(index)] != nullptr)
        {
            auto* header = filterSections[size_t(index)]->header.get();
            header->setVisible(true);
            rows[3].push_back({header});
        }
    }

    const juce::Rectangle<int> gridBounds {inset, controlsTop, innerWidth, 0};
    std::array<juce::Rectangle<int>, 4> placements;
    auto rowTop = controlsTop;
    auto contentBottom = controlsTop;
    for (size_t index = 0; index < rows.size(); ++index)
    {
        if (rows[index].empty()) continue;
        placements[index] = measureControlGrid(*this, gridBounds.withY(rowTop), rows[index]);
        contentBottom = placements[index].getBottom();
        rowTop = contentBottom + getUiRowGap(*this);
    }
    const auto moduleHeight = contentBottom + gap + controlBlockFrameThickness;

    filterContent.controlBlockFrames.clear();
    filterContent.setInterceptsMouseClicks(false, true);
    filterContent.setBounds(0, 0, nestedWidth, moduleHeight);
    for (auto& section : filterSections)
        if (section != nullptr)
        {
            section->body.controlBlockFrames.clear();
            section->body.setInterceptsMouseClicks(false, true);
            section->body.setBounds(0, 0, nestedWidth, moduleHeight);
        }

    eqlContent.setSize(nestedWidth, moduleHeight);
    for (size_t index = 0; index < rows.size(); ++index)
        if (!placements[index].isEmpty())
            layoutControlGrid(*this, gridBounds.withY(placements[index].getY()), rows[index]);
    eqlContent.controlBlockFrames.push_back({{0, 0, frameWidth, moduleHeight}, "EQL", uiGreyDark});
    filterViewport.setViewPosition(0, filterViewport.getViewPositionY());
}

void AvaAudioProcessorEditor::handleEqlBackgroundClick(const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

void AvaAudioProcessorEditor::layoutUnifiedBlocks(juce::Rectangle<int>& bounds)
{
    const auto scroll = filterViewport.getViewPositionY();
    layoutEqlModuleSections(bounds);
    const auto width = filterViewport.getWidth();
    const auto instanceInset = getUiGap(*this) + controlBlockFrameThickness;
    // The white frame keeps its existing 560px width; only its inner inset
    // grows by the border thickness to expose exactly 8px of background.
    const auto blockWidth = width - getUiGap(*this);
    const auto innerBlockWidth = blockWidth - 2 * instanceInset;
    const auto innerViewportWidth = width - 2 * instanceInset;
    auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get());
    const auto bands = crossover != nullptr ? crossover->getMainControls() : std::vector<juce::Component*>{};
    const auto sync = crossover != nullptr ? crossover->getMainSyncControls()
        : std::array<juce::Component*, 2>{};
    const auto settings = crossover != nullptr ? crossover->getInstanceMainRows() : InstanceMainRows{};

    const auto unitCollapsed = unitContent.isBlockCollapsed("UNIT");
    const auto paddingUnit = controlBlockPadding + controlBlockFrameThickness;
    const auto unitGap = getUiGap(*this);
    const auto squareSize = getSquareControlSize(*this);
    int unitHeight = controlBlockHeaderHeight;
    if (!unitCollapsed)
    {
        // Each UNIT child has its own persisted collapse state. Recompute all
        // heights on every reflow, so folding a nested header closes its space.
        oscPanel->setSize(innerBlockWidth, squareSize);
        const auto oscHeight = oscPanel->getPreferredHeight();
        const auto syncCollapsed = unitSyncContent.isBlockCollapsed("SYNC");
        std::vector<ControlGridCell> syncCells {{unitNumberControl.get()}, {receiveControl.get()}, {sendControl.get()}, {linksCountControl.get()}};
        for (int index = 0; index < linkDestinationCount; ++index)
            syncCells.push_back({linkToControls[size_t(index)].get()});
        const auto syncHeight = syncCollapsed ? controlBlockHeaderHeight
            : measureControlGrid(unitSyncContent,
                {paddingUnit, controlBlockHeaderHeight + paddingUnit,
                 innerBlockWidth - 2 * paddingUnit, 0}, syncCells).getBottom() + paddingUnit;
        int blockY = controlBlockHeaderHeight + unitGap;
        const std::array<juce::Component*, 17> controls {
            routingControl.get(), slotsControl.get(), undoButton.get(), redoButton.get(), abControl.get(), abCopyButton.get(),
            globalBypassButton.get(), bypassCompensationControl.get(),
            unitCopyButton.get(), unitPasteButton.get(), unitZoomControl.get(), unitWidthControl.get(), unitDockSizeControl.get(),
            pinRoutingControl.get(), unitSearchControl.get(), unitSampleRateLabel.get(), aboutControl.get()
        };
        unitMainRow.layout({instanceInset, blockY, innerBlockWidth, controlBlockHeaderHeight}, controls);
        blockY = unitMainContent.getBottom() + unitGap;
        oscPanel->setBounds(instanceInset, blockY, innerBlockWidth, oscHeight);
        blockY += oscHeight + unitGap;
        unitSyncContent.setBounds(instanceInset, blockY, innerBlockWidth, syncHeight);
        unitNumberControl->setVisible(!syncCollapsed);
        sendControl->setVisible(!syncCollapsed);
        receiveControl->setVisible(!syncCollapsed);
        linksCountControl->setVisible(!syncCollapsed);
        for (size_t index = 0; index < linkToControls.size(); ++index)
        {
            const bool visible = !syncCollapsed && int(index) < linkDestinationCount;
            linkToControls[index]->setVisible(visible);
            if (!visible) linkToControls[index]->setBounds({});
        }
        if (!syncCollapsed)
            layoutControlGrid(unitSyncContent,
                {paddingUnit, controlBlockHeaderHeight + paddingUnit,
                 innerBlockWidth - 2 * paddingUnit, squareSize}, syncCells);
        else
        {
            unitNumberControl->setBounds({});
            sendControl->setBounds({});
            receiveControl->setBounds({});
            linksCountControl->setBounds({});
        }
        unitSyncContent.controlBlockFrames = {
            {{0, 0, innerBlockWidth, syncHeight}, "SYNC", uiGreyDark}};
        unitHeight = blockY + syncHeight + unitGap + controlBlockFrameThickness;
    }
    unitMainContent.setVisible(!unitCollapsed);
    unitMainRow.syncScrollBar();
    oscPanel->setVisible(!unitCollapsed);
    unitSyncContent.setVisible(!unitCollapsed);
    unitContent.setBounds(0, 0, blockWidth, unitHeight);
    unitContent.controlBlockFrames = {
        {{0, 0, blockWidth, unitHeight}, "UNIT", uiWhite, {}, true}};
    unitContent.setVisible(true);
    const auto instanceY = unitHeight + unitGap;

    std::vector<juce::Component*> instanceControls {
        linkInstanceControl.get(), settings.secondary[0], settings.secondary[1],
        sync[0], sync[1], settings.secondary[2]
    };
    instanceControls.insert(instanceControls.end(), settings.splits.begin(), settings.splits.end());
    instanceControls.insert(instanceControls.end(), bands.begin(), bands.end());
    instanceControls.insert(instanceControls.end(), settings.actions.begin(), settings.actions.end());
    std::erase(instanceControls, nullptr);
    instanceMainRow.layout({instanceInset, controlBlockHeaderHeight + unitGap,
                            innerBlockWidth, controlBlockHeaderHeight}, instanceControls);

    // The INSTANCE frame now contains MAIN and the entire module stack.  Set
    // the width before reparenting modules, so children can size themselves.
    instanceContent.setBounds(0, instanceY, blockWidth, controlBlockHeaderHeight);
    ensureModuleList();
    const auto modulesY = mainContent.getBottom() + unitGap;
    if (moduleList != nullptr)
    {
        const juce::Rectangle<int> boundsForModules {instanceInset, modulesY, innerBlockWidth,
                                                     moduleList->getContentHeight()};
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
        editor->getProperties().set("contentRightInset", getUiGap(*this));
        if (editor->getParentComponent() != &instanceContent) instanceContent.addChildComponent(*editor);
        editor->setBounds(0, 0, innerViewportWidth, 1);
        editor->setSize(innerViewportWidth, editor->getPreferredHeight());
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
            editor->getProperties().set("contentRightInset", getUiGap(*this));
            if (editor->getParentComponent() != &instanceContent) instanceContent.addChildComponent(*editor);
            editor->setBounds(0, 0, innerViewportWidth, 1);
            editor->setSize(innerViewportWidth, editor->getPreferredHeight());
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
        found->second->updateLayout(innerViewportWidth);
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

    const auto instanceCollapsed = instanceContent.isBlockCollapsed("INSTANCE");
    mainContent.setVisible(!instanceCollapsed);
    instanceMainRow.syncScrollBar();
    if (moduleList != nullptr) moduleList->setVisible(!instanceCollapsed);
    // All module blocks are children of INSTANCE, so a single collapse hides
    // the complete stack.  Each module still owns its individual collapse key.
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
        if (auto* page = dynamic_cast<ControlBlockComponent*>(block))
            page->setModuleNameplateBypass(readBypass, toggleBypass);
        else if (auto* dyn = dynamic_cast<CrossoverModuleComponent*>(block))
            dyn->setModuleNameplateBypass(readBypass, toggleBypass);
        const auto type = size_t(slot.type);
        auto caption = juce::String(module_order::descriptions[type].name).toUpperCase();
        if (shownCounts[type] > 1)
            caption += " (" + juce::String(++shownOrdinals[type]).paddedLeft('0', 2) + ")";
        if (slot.type == module_order::dyn)
        {
            if (auto* dynEditor = dynamic_cast<CrossoverModuleComponent*>(block))
                dynEditor->setModuleDisplayTitle(caption);
        }
        else if (auto* blockFrame = dynamic_cast<ControlBlockComponent*>(block))
        {
            for (auto& frame : blockFrame->controlBlockFrames)
                if (frame.title != "INSTANCE" && frame.title != "MAIN")
                    frame.displayTitle = caption;
            blockFrame->repaint();
        }
        y += getUiGap(*this);
        block->setVisible(!instanceCollapsed);
        block->setTopLeftPosition(instanceInset, y);
        y = block->getBottom();
    }
    const int instanceHeight = instanceCollapsed ? controlBlockHeaderHeight
        : y + getUiGap(*this) + controlBlockFrameThickness;
    instanceContent.setBounds(0, instanceY, blockWidth, instanceHeight);
    instanceContent.controlBlockFrames = {{{0, 0, blockWidth, instanceHeight}, "INSTANCE", uiWhite}};
    updateInstanceHeadings();
    instanceContent.setVisible(true);
    moduleContent.setSize(width, juce::jmax(filterViewport.getHeight(), instanceY + instanceHeight));
    // Preserve the user's scroll position after ADD, DEL and ON/OFF.
    // In particular, never jump to a newly created module automatically.
    filterViewport.setViewPosition(0, scroll);
    updatePinnedSearchPosition();
    refreshQuickAccessControls();
}

void AvaAudioProcessorEditor::updatePinnedSearchPosition()
{
    if (controlSearchPanel == nullptr || updatingPinnedSearchPosition) return;
    const juce::ScopedValueSetter<bool> guard(updatingPinnedSearchPosition, true);
    const auto* visibleEditor = activeInstanceEditor != nullptr ? activeInstanceEditor.get() : this;
    const bool showingEditor = !isDockOnly() && !visibleEditor->hasUnitPage()
        && filterViewport.isVisible();
    const bool searchEnabled = shell_editor_ui::isSearchVisible(getRootWindowState());
    const bool showSearch = showingEditor && activeInstanceEditor == nullptr && searchEnabled;
    if (unitSearchControl != nullptr && unitSearchControl->getSelectedChoiceIndex() != int(searchEnabled))
        unitSearchControl->setSelectedChoiceIndex(searchEnabled ? 1 : 0, false);
    controlSearchPanel->setVisible(showSearch);
    const auto searchHeight = visibleEditor->controlSearchPanel != nullptr
        ? visibleEditor->controlSearchPanel->getDisplayedHeight()
        : controlSearchPanel->getDisplayedHeight();

    // PIN ROUTING belongs to UNIT. Keep a single visible strip on the root
    // editor as INSTANCE editors are created and destroyed. Nested editors
    // reserve the same height, but do not paint a second strip.
    const bool routingEnabled = showingEditor
        && bool(getRootWindowState().getProperty(shell_editor_ui::key("pin_routing"), true));
    const bool reserveRouting = routingEnabled;
    const bool showRootRouting = reserveRouting && routingInstanceId == 0;
    const bool showUnitStatus = showRootRouting
        && (!searchEnabled || searchHeight == controlBlockHeaderHeight);
    unitSyncPlate.setVisible(showUnitStatus);
    if (!showRootRouting) pinnedRoutingViewport.setVisible(false);
    if (!showingEditor) return;

    const auto gap = getUiGap(*this);
    const auto searchTop = 2 * gap;
    const auto x = visibleEditor->filterViewport.getX();
    const auto width = visibleEditor->filterViewport.getWidth();
    const auto unitWidth = juce::jmax(0, width - gap);
    const auto routingTop = searchTop + (searchEnabled ? searchHeight + gap : 0);
    const auto tileHeight = getSquareControlSize(*this);
    const auto pinnedZoneHeight = tileHeight + gap;
    int routingInset = 0;
    if (showUnitStatus)
    {
        constexpr auto indicatorSize = unitIndicatorSize;
        const auto baseFont = makeUiFont().withHeight(float(indicatorSize));
        const auto lineHeight = juce::jmin(tileHeight / 4, tileHeight - 3 * indicatorSize);
        const auto numberFont = baseFont.withPointHeight(baseFont.getHeightInPoints() + 4.0f);
        unitNumberLabel.setFont(numberFont.withHeight(juce::jmin(float(lineHeight), numberFont.getHeight())));
        const auto columnWidth = juce::jmax(indicatorSize, getTextPixelWidth(unitNumberLabel.getFont(), "88") + 2);
        unitSyncPlate.setBounds(x, routingTop, columnWidth, tileHeight);
        routingInset = columnWidth + gap;
        const auto freeHeight = tileHeight - 3 * indicatorSize - lineHeight;
        const auto spacing = [freeHeight](int gaps) { return juce::roundToInt(freeHeight * gaps / 3.0); };
        const auto square = [&](int y)
        {
            return juce::Rectangle<int>((columnWidth - indicatorSize) / 2, y, indicatorSize, indicatorSize);
        };
        unitReceiveIndicator.setBounds(square(0));
        unitSendIndicator.setBounds(square(indicatorSize + spacing(1)));
        unitNumberLabel.setBounds(1, 2 * indicatorSize + spacing(2), columnWidth, lineHeight);
        unitSyncPauseToggle.setBounds(square(tileHeight - indicatorSize));
    }
    if (showSearch)
    {
        controlSearchPanel->setPinned(filterViewport.getViewPositionY() > 0);
        controlSearchPanel->setBounds(x, searchTop, unitWidth, searchHeight);
    }
    if (showRootRouting)
    {
        pinnedRoutingViewport.setBounds(x + routingInset, routingTop, juce::jmax(0, unitWidth - routingInset), pinnedZoneHeight);
        // Never expose a previously hidden ribbon at stale coordinates.
        pinnedRoutingViewport.setVisible(true);
        pinnedRoutingViewport.toFront(false);
    }
    else pinnedRoutingViewport.setBounds({});
    if (showUnitStatus) unitSyncPlate.toFront(false);

    if (activeInstanceEditor != nullptr) return;
    const auto top = routingTop + (reserveRouting ? pinnedZoneHeight : 0);
    const auto bottom = quickAccessStrip.getY();
    const juce::Rectangle<int> visibleBounds{x, top, width, juce::jmax(0, bottom - top)};
    if (filterViewport.getBounds() != visibleBounds)
        filterViewport.setBounds(visibleBounds);
    if (showSearch) controlSearchPanel->toFront(false);
    if (showRootRouting) pinnedRoutingViewport.toFront(false);
}
