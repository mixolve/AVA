#include "EqlCopyEditor.h"
#include "ModuleList.h"
#include "Editor.h"
#include "EditorStateSchema.h"
#include "LookAndFeel.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "../crossover/Editor.h"
#include "../routing/Panel.h"
#include "OscPanel.h"

#include <cmath>

AvaAudioProcessorEditor::~AvaAudioProcessorEditor()
{
    // A deferred mouse-up may still be pending when the editor closes.
    if (hostStateActionTracker != nullptr)
    {
        removeMouseListener(hostStateActionTracker.get());
        hostStateActionTracker.reset();
    }
    cancelFilterDrag();
    audioProcessor.setContentEditor(nullptr);
    commitPendingHistorySnapshot(true);
    unregisterParameterListeners();
    storeEditorStateToValueTree();
    stopTimer();
    pinnedRoutingViewport.onViewChanged = {};
    quickAccessViewport.onViewChanged = {};
    pinnedRoutingViewport.setViewedComponent(nullptr, false);
    slotsViewport.setViewedComponent(nullptr, false);
    setLookAndFeel(nullptr);
}

void AvaAudioProcessorEditor::timerCallback()
{
    // The host may open the plugin without focusing any JUCE component.
    // Claim focus once after attachment, without interrupting an active child
    // editor, prompt, or an already focused search box.
    if (initialKeyboardFocusPending && isShowing()
        && shell_editor_ui::hasPluginKeyboardFocus(*this))
    {
        if (!hasKeyboardFocus(true) && (textPromptOverlay == nullptr || !textPromptOverlay->isVisible()))
            grabKeyboardFocus();
        // A DAW can deny the first focus request before its plugin window is
        // activated; retry only while this editor is the active plugin window.
        initialKeyboardFocusPending = hasKeyboardFocus(true) == false;
    }
    refreshInstanceLinkControls();
    refreshPinnedRoutingStrip();
    syncQuickAccessControls();
    if (unitSampleRateLabel != nullptr)
    {
        const auto sampleRate = audioProcessor.getRoutingOwner().getSampleRate();
        juce::String value = "—";
        if (sampleRate > 0.0 && std::isfinite(sampleRate))
        {
            const auto latencySamples = audioProcessor.getRoutingOwner().getLatencySamples();
            value = juce::String(juce::roundToInt(sampleRate)) + "\n"
                  + juce::String(latencySamples);
        }
        if (unitSampleRateLabel->getText() != value)
            unitSampleRateLabel->setText(value, juce::dontSendNotification);
    }
    if (unitWidthControl != nullptr)
        unitWidthControl->setSelectedChoiceIndex(
            shell_editor_ui::editorColumns(getRootWindowState()) - shell_editor_ui::minEditorColumns, false);
    if (unitDockSizeControl != nullptr)
        unitDockSizeControl->setSelectedChoiceIndex(shell_editor_ui::dockControlCount(getRootWindowState()) - 1, false);
    if (unitZoomControl != nullptr)
    {
        const auto zoom = shell_editor_ui::zoomChoiceIndex(shell_editor_ui::zoomStep(getRootWindowState()));
        if (unitZoomControl->getSelectedChoiceIndex() != zoom)
            unitZoomControl->setSelectedChoiceIndex(zoom, false);
    }
    if (activeInstanceId != 0
        && audioProcessor.getRoutingInstanceHandle(activeInstanceId) == nullptr)
        closeRoutingInstance();

    const auto revision = audioProcessor.getStateRevision();
    if (!audioProcessor.isRoutingInstance() && revision != observedStateRevision) {
        observedStateRevision = revision;
        pendingHistorySnapshot.store(true);
        lastHistoryChangeTimeMs.store(juce::Time::getMillisecondCounter());
    }
    commitPendingHistorySnapshot();
    if (audioProcessor.isRoutingInstance()) updateUndoRedoButtons();

    const auto activeModule = audioProcessor.getActiveModule();
    const auto moduleStateChanged = displayedModule != activeModule;

    if (moduleStateChanged)
        resyncEditorFromProcessorState();

    updateInstanceScrollLabel();
    if (moduleList != nullptr && filterViewport.isVisible())
        moduleList->refreshItemsIfChanged();
    const auto clipEventCount = audioProcessor.getGlobalClipEventCount();
    const auto now = juce::Time::getMillisecondCounter();
    updateSearchFlash();

    {
        if (clipEventCount != lastClipEventCount)
        {
            lastClipEventCount = clipEventCount;
            lastClipIndicatorTimeMs = now;
        }

        // Lit while the output is clipping, then released 50 ms after the last
        // clipped block instead of sitting on a fixed hold.
        constexpr uint32_t clipIndicatorReleaseMs = 50;
        const auto showClipIndicator = lastClipIndicatorTimeMs != 0
            && now - lastClipIndicatorTimeMs < clipIndicatorReleaseMs;

        if (showClipIndicator != clipIndicatorShown)
        {
            clipIndicatorShown = showClipIndicator;
            dockControls.clip.setActive(showClipIndicator);
        }
    }

    if (auto* blockChainEditor = dynamic_cast<CrossoverModuleComponent*>(blockChainModuleEditor.get()); blockChainEditor != nullptr && filterViewport.isVisible())
        blockChainEditor->refreshExternalState();

    if (auto* dynEditor = dynamic_cast<CrossoverModuleComponent*>(dynModuleEditor.get()); dynEditor != nullptr && filterViewport.isVisible())
        dynEditor->refreshExternalState();

    if (auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()); editor != nullptr && editor->isVisible())
        editor->refreshExternalState();
    for (auto& [uid, component] : copiedModuleEditors)
    {
        juce::ignoreUnused(uid);
        if (auto* editor = dynamic_cast<CrossoverModuleComponent*>(component.get()))
            editor->refreshExternalState();
    }

    if (oscPanel != nullptr)
        oscPanel->refresh();

}

bool AvaAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (!key.isKeyCode(juce::KeyPress::returnKey)) return false;

    // A prompt, text/numeric edit or search input owns its Enter key. Do not
    // commit an edit and open global search with the same keystroke.
    if (textPromptOverlay != nullptr && textPromptOverlay->isVisible()) return false;
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<juce::TextEditor*>(focused) != nullptr) return false;

    if (hostSlotMoveSourceIndex >= 0)
    {
        cancelHostSlotMove();
        return true;
    }

    if (activeInstanceEditor != nullptr && activeInstanceEditor->isShowing())
        return activeInstanceEditor->keyPressed(key);

    // Enter is a search shortcut on ALL and INFO alike; navigating
    // from another page first reveals the existing pinned search control.
    if (hasUnitPage()) showUnitPage(UnitPage::main);
    if (controlSearchPanel != nullptr && controlSearchPanel->isShowing()
        && !controlSearchPanel->hasKeyboardFocus(true))
    {
        controlSearchPanel->focusSearch();
        return true;
    }
    return false;
}

void AvaAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    if (hostSlotMoveSourceIndex >= 0) cancelHostSlotMove();
    grabKeyboardFocus();
}

void AvaAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    if (hasUnitPage() && !event.mouseWasDraggedSinceMouseDown()
        && !event.mods.isPopupMenu() && getLocalBounds().contains(event.getPosition()))
        showUnitPage(UnitPage::main);
}
