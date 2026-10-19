#include "Editor.h"
#include "LookAndFeel.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "../crossover/Editor.h"
#include "../routing/Panel.h"
#include "OscPanel.h"

#include <cmath>

AvaAudioProcessorEditor::~AvaAudioProcessorEditor()
{
#if JUCE_MAC
    svg_export::removeKeyboardShortcut(svgExportKeyboardMonitor);
#endif
    animationSettingsWindow.reset();
    audioProcessor.setOscActionEditor(nullptr);
    oscParameterListWindow.reset();
    commitPendingHistorySnapshot(true);
    unregisterParameterListeners();
    storeEditorStateToValueTree();
    stopTimer();
    setLookAndFeel(nullptr);
}

void AvaAudioProcessorEditor::timerCallback()
{
    const auto location = audioProcessor.refreshHostTrackName();
    animationSettings->setHostName(location.text, location.onItem, location.itemName);
    if (hostSlotControlDrag && ! juce::ModifierKeys::getCurrentModifiersRealtime().isCtrlDown())
        cancelHostSlotMove();

    if (activeInstanceId != 0
        && audioProcessor.getRoutingInstanceHandle(activeInstanceId) == nullptr)
        closeRoutingInstance();

    commitPendingHistorySnapshot();

    const auto activeModule = audioProcessor.getActiveModule();
    const auto moduleStateChanged = eqlModuleLoaded != (activeModule == AvaAudioProcessor::ActiveModule::eql)
        || tlsModuleLoaded != (activeModule == AvaAudioProcessor::ActiveModule::tls)
        || dynModuleLoaded != (activeModule == AvaAudioProcessor::ActiveModule::dyn);

    if (moduleStateChanged)
        resyncEditorFromProcessorState();

    updateInstanceHeadings();

    const auto clipValue = audioProcessor.getGlobalClipIndicator();
    const auto now = juce::Time::getMillisecondCounter();

    if (clipButton != nullptr)
    {
        if (clipValue > 0.5f)
            lastClipIndicatorTimeMs = now;

        constexpr uint32_t clipIndicatorHoldMs = 500;
        const auto showClipIndicator = lastClipIndicatorTimeMs != 0
            && now - lastClipIndicatorTimeMs < clipIndicatorHoldMs;

        if (showClipIndicator)
            clipButton->setTextColourOverride(uiWhite);
        else
            clipButton->setTextColourOverride(uiBackground);
    }

    if (auto* tlsEditor = dynamic_cast<CrossoverModuleComponent*>(tlsModuleEditor.get()))
        tlsEditor->refreshExternalState();

    if (auto* dynEditor = dynamic_cast<CrossoverModuleComponent*>(dynModuleEditor.get()))
        dynEditor->refreshExternalState();

    if (auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
        editor->refreshExternalState();

    if (oscPanel != nullptr)
        oscPanel->refresh();

}

bool AvaAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (isSvgExportGesture(key.getModifiers())
        && (key.isKeyCode('E') || key.isKeyCode('e')))
    {
        requestSvgExport();
        return true;
    }

    if (hostSlotMoveSourceIndex >= 0
        && (key.isKeyCode(juce::KeyPress::escapeKey) || key.isKeyCode(juce::KeyPress::returnKey)))
    {
        cancelHostSlotMove();
        return true;
    }

    if (! key.isKeyCode(juce::KeyPress::escapeKey))
        return false;

    if (activeInstanceEditor != nullptr)
        return activeInstanceEditor->keyPressed(key);
    if (textPromptOverlay != nullptr)
        return false;
    if (routingPanel != nullptr && routingPanel->cancelMoveMode())
        return true;

    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused != nullptr && (dynamic_cast<const juce::TextEditor*>(focused) != nullptr
        || focused->findParentComponentOfClass<juce::TextEditor>() != nullptr))
        return false;
    if (juce::Desktop::getInstance().getNumDraggingMouseSources() > 0)
        return false;

    if (returnToRoutingAction != nullptr)
    {
        const auto action = returnToRoutingAction;
        action();
    }
    else if (! routingExpanded)
        toggleRoutingSection();
    return true;
}

void AvaAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (hostSlotMoveSourceIndex >= 0) cancelHostSlotMove();
    clearKeyboardFocus(*this);
    if(event.mods.isPopupMenu() && audioProcessor.getActiveModule()==AvaAudioProcessor::ActiveModule::tls)
        if(auto* module=dynamic_cast<CrossoverModuleComponent*>(tlsModuleEditor.get())) module->showBlockArrange();
}

void AvaAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (hostParametersViewport.getBounds().contains(event.getPosition()))
    {
        if (scrollViewportWithWheel(hostParametersViewport, hostParametersContent.getHeight(), wheel, event.mods.isShiftDown()))
        {
            storeEditorUiState();
            return;
        }
    }

    if (! filterViewport.getBounds().contains(event.getPosition()))
        return;

    scrollViewportWithWheel(filterViewport, getActiveFilterContentHeight(), wheel, event.mods.isShiftDown());
    storeEditorUiState();
}
