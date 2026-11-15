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
    pinnedRoutingViewport.setViewedComponent(nullptr, false);
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
    const auto scrollUntil = double(mainScrollBar.getProperties().getWithDefault("scrollActiveUntil", 0.0));
    if (scrollUntil > 0.0 && juce::Time::getMillisecondCounterHiRes() >= scrollUntil)
    {
        mainScrollBar.getProperties().remove("scrollActiveUntil");
        mainScrollBar.repaint();
    }
    const auto fadeUntil = double(mainScrollBar.getProperties().getWithDefault("scrollFadeUntil", 0.0));
    if (fadeUntil > 0.0)
    {
        mainScrollBar.repaint();
        if (juce::Time::getMillisecondCounterHiRes() >= fadeUntil)
            mainScrollBar.getProperties().remove("scrollFadeUntil");
    }
    if (unitSampleRateLabel != nullptr)
    {
        // The host determines the processor sample rate; no local setting is
        // written and no parameter is exposed for this informational readout.
        const auto sampleRate = audioProcessor.getRoutingOwner().getSampleRate();
        juce::String value = "—";
        if (sampleRate > 0.0 && std::isfinite(sampleRate))
            value = juce::String(juce::roundToInt(sampleRate));
        if (unitSampleRateLabel->getText() != value)
            unitSampleRateLabel->setText(value, juce::dontSendNotification);
    }
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

    updateInstanceHeadings();
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
            dockClipIndicator.setActive(showClipIndicator);
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

    // Nested routing instance editors have their own search/ALL page.
    if (activeInstanceEditor != nullptr && activeInstanceEditor->isShowing())
        return activeInstanceEditor->keyPressed(key);

    // A disabled UNIT search must not intercept Enter or close ABOUT.
    if (!bool(getRootWindowState().getProperty(shell_editor_ui::key("search_visible"), true)))
        return false;
    // Enter is a search shortcut on ALL and INFO alike; navigating
    // from another page first reveals the existing pinned search control.
    if (aboutExpanded) showAllSection();
    if (controlSearchPanel != nullptr && controlSearchPanel->isShowing()
        && !controlSearchPanel->hasKeyboardFocus(true))
    {
        controlSearchPanel->focusSearch();
        return true;
    }
    return false;
}

void AvaAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (aboutExpanded && (aboutPanel == nullptr
        || !aboutPanel->getBounds().contains(event.getPosition())))
    {
        showAllSection();
        return;
    }
    if (hostSlotMoveSourceIndex >= 0) cancelHostSlotMove();
    grabKeyboardFocus();
}

bool AvaAudioProcessorEditor::allowsViewportScroll(const juce::Component& viewport) const
{
    for (auto* editor = this; editor != nullptr;
         editor = editor->findParentComponentOfClass<AvaAudioProcessorEditor>())
        if (const auto* popup = editor->textPromptOverlay.get(); popup != nullptr && popup->isVisible()
            && popup != &viewport && !popup->isParentOf(&viewport)) return false;
    if (activeInstanceEditor != nullptr && !activeInstanceEditor->isParentOf(&viewport))
        return activeInstanceEditor->allowsViewportScroll(viewport);
    return true;
}

void AvaAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!allowsViewportScroll(*this)) return;
    if (! filterViewport.getBounds().contains(event.getPosition()))
        return;

    scrollViewportWithWheel(filterViewport, getActiveFilterContentHeight(), wheel, event.mods.isShiftDown());
    storeEditorUiState();
}
