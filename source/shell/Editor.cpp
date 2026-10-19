#include "ChoiceControl.h"
#include "Editor.h"
#include "EditorStateSchema.h"
#include "SvgExport.h"
#include "AnimationState.h"
#include "EdgeResizeHandle.h"
#include "LookAndFeel.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "EditorSetup.h"
#include "../routing/Panel.h"
#include "../routing/State.h"
#include "OscPanel.h"
#include "../modules/eql/Processor.h"

#include <algorithm>

namespace
{
juce::MemoryBlock makeNoModuleSnapshotFrom(const juce::MemoryBlock& sourceSnapshot)
{
    juce::MemoryBlock noModuleSnapshot;
    auto stateXml = AvaAudioProcessor::getXmlFromBinary(sourceSnapshot.getData(),
                                                       static_cast<int>(sourceSnapshot.getSize()));

    if (stateXml == nullptr)
        return noModuleSnapshot;

    auto state = juce::ValueTree::fromXml(*stateXml);
    AvaAudioProcessor::removeModuleStateProperties(state);

    if (auto noModuleXml = state.createXml())
        AvaAudioProcessor::copyXmlToBinary(*noModuleXml, noModuleSnapshot);

    return noModuleSnapshot;
}

}

void AvaAudioProcessorEditor::requestSvgExport()
{
    auto* editor = this;
    while (auto* parent = editor->findParentComponentOfClass<AvaAudioProcessorEditor>())
        editor = parent;
    svg_export::chooseFile(*editor);
}

bool AvaAudioProcessorEditor::isSvgExportGesture(const juce::ModifierKeys& modifiers)
{
    return modifiers.isCtrlDown() && modifiers.isAltDown() && modifiers.isShiftDown()
#if JUCE_MAC
        && !modifiers.isCommandDown()
#endif
        ;
}

AvaAudioProcessorEditor::AvaAudioProcessorEditor(AvaAudioProcessor& processorToEdit,
                                                 const int routingInstanceIdIn)
    : AudioProcessorEditor(&processorToEdit),
      audioProcessor(processorToEdit),
      valueTreeState(processorToEdit.getValueTreeState()),
      lookAndFeel(std::make_unique<AvaLookAndFeel>()),
      routingInstanceId(routingInstanceIdIn)
{
    setName("editor");
#if JUCE_MAC
    svgExportKeyboardMonitor = svg_export::installKeyboardShortcut(*this);
#endif
    const auto restoredInstanceId = static_cast<int>(valueTreeState.state.getProperty(shell_editor_ui::key("active_instance"), 0));
    loadAnimationSettings(*animationSettings, valueTreeState.state);
    const auto location = audioProcessor.refreshHostTrackName();
    animationSettings->setHostName(location.text, location.onItem, location.itemName);
    audioProcessor.setOscActionEditor(this);

    setLookAndFeel(lookAndFeel.get());
    setOpaque(true);
    setResizable(true, false);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    hostParametersViewport.setViewedComponent(&hostParametersContent, false);
    hostParametersViewport.setName("host-viewport");
    hostParametersContent.setName("host-content");
    if (auto* holder = hostParametersContent.getParentComponent()) holder->setName("host-scroll");
    hostParametersViewport.setScrollBarsShown(false, true);
    hostParametersViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    hostParametersViewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(hostParametersViewport);
    routingPanel = std::make_unique<RoutingPanel>(audioProcessor);
    routingPanel->setOnOpenRoot([this]
    {
        toggleRoutingSection();
    });
    if (! audioProcessor.isRoutingInstance())
    {
        routingPanel->setOnOpenInstance([this] (const int id)
        {
            openRoutingInstance(id);
        });
        routingPanel->setOnTopologyChanged([this]
        {
            const auto topology = ava::routing::readState(valueTreeState.state);
            if (activeInstanceId != 0
                && (activeInstanceId == topology.rootInstanceId
                    || std::none_of(topology.nodes.begin(), topology.nodes.end(),
                                    [this] (const auto& node) { return node.id == activeInstanceId; })))
                closeRoutingInstance();
            audioProcessor.synchronizeRouting();
        });
    }
    routingPanel->setOnRenameRequest([this] (const int id, const juce::String& currentName,
                                             juce::Component& anchor)
    {
        showTextPrompt(currentName,
                       [this, id] (const juce::String& newName)
                       {
                           return ava::routing::renameInstance(valueTreeState.state, id, newName);
                       },
                       getLocalArea(&anchor, anchor.getLocalBounds()), {}, {}, true);
    });
    routingPanel->setVisible(false);
    addAndMakeVisible(*routingPanel);
    OscPanel::Actions oscActions;
    oscActions.getSettings = [this] { return audioProcessor.getOscSettings(); };
    oscActions.setSettings = [this] (const OscSettings& settings)
    {
        return audioProcessor.setOscSettings(settings);
    };
    oscActions.isInputPortBusy = [this] { return audioProcessor.isOscInputPortBusy(); };
    oscActions.toggleParameterList = [this] { showOscParameterList(); };
    oscActions.isParameterListVisible = [this] { return oscParameterListWindow != nullptr; };
    oscActions.showTextPrompt = [this] (juce::Component& anchor,
                                        const juce::String& currentText,
                                        std::function<bool(const juce::String&)> onCommit)
    {
        showTextPrompt(currentText, std::move(onCommit),
                       getLocalArea(&anchor, anchor.getLocalBounds()));
    };
    oscActions.clearFocus = [this] { clearKeyboardFocus(*this); };
    oscPanel = std::make_unique<OscPanel>(std::move(oscActions));
    oscPanel->setVisible(false);
    addAndMakeVisible(*oscPanel);
    filterViewport.setViewedComponent(&filterContent, false);
    filterViewport.setName("filter-viewport");
    filterContent.setName("filter-content");
    if (auto* holder = filterContent.getParentComponent()) holder->setName("filter-scroll");
    filterViewport.setScrollBarsShown(false, false);
    filterViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    filterViewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(filterViewport);

    verticalResizeHandle = std::make_unique<EdgeResizeHandle>(
        *this, EdgeResizeHandle::Axis::vertical, minimumEditorHeight, maximumEditorHeight);
    addAndMakeVisible(*verticalResizeHandle);
    horizontalResizeHandle = std::make_unique<EdgeResizeHandle>(
        *this, EdgeResizeHandle::Axis::horizontal, minimumEditorWidth, maximumEditorWidth);
    addAndMakeVisible(*horizontalResizeHandle);
    topResizeHandle = std::make_unique<EdgeResizeHandle>(
        *this, EdgeResizeHandle::Axis::vertical, minimumEditorHeight, maximumEditorHeight, true);
    addAndMakeVisible(*topResizeHandle);
    leftResizeHandle = std::make_unique<EdgeResizeHandle>(
        *this, EdgeResizeHandle::Axis::horizontal, minimumEditorWidth, maximumEditorWidth, true);
    addAndMakeVisible(*leftResizeHandle);
    cornerResizeHandle = std::make_unique<EdgeResizeHandle>(
        *this, EdgeResizeHandle::Axis::both, minimumEditorWidth, maximumEditorWidth);
    addAndMakeVisible(*cornerResizeHandle);

    clipButton = std::make_unique<BoxTextButton>(uiAccent);
    clipButton->setButtonText({});
    clipButton->setTablerIcon("wave-square");
    clipButton->setTextJustification(juce::Justification::centred);
    clipButton->setClickingTogglesState(false);
    clipButton->setFillVisible(false);
    clipButton->setInterceptsMouseClicks(false, false);
    clipButton->setTextColourOverride(uiBackground);
    addAndMakeVisible(*clipButton);

    hostButton = std::make_unique<BoxTextButton>(uiAccent);
    hostButton->setName("host");
    hostButton->setButtonText({});
    hostButton->setTablerIcon("map-pin-share");
    hostButton->setTextJustification(juce::Justification::centred);
    hostButton->setClickingTogglesState(true);
    hostButton->setToggleActiveFillEnabled(true);
    hostButton->onClick = [this]
    {
        toggleHostParametersSection();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*hostButton);

    moduleAddButton = std::make_unique<BoxTextButton>(uiAccent);
    moduleAddButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscAddModuleId);
    moduleAddButton->setButtonText("ADD-MODULE");
    moduleAddButton->setTextJustification(juce::Justification::centred);
    moduleAddButton->onClick = [this]
    {
        showModulePicker();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*moduleAddButton);

    setLoadedModuleFlags(audioProcessor.getActiveModule());

    setupShellControls();

    setupPresetControls();

    filterDisplayOrder.reserve(EqlModuleProcessor::maxFilterCount);
    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
        filterDisplayOrder.push_back(filterIndex);

    if (auto* initialEqlProcessor = audioProcessor.getEqlModuleProcessor())
        setupEqlControls(initialEqlProcessor->getValueTreeState());

    restoreEditorStateFromValueTree();

    footerTab = std::make_unique<BoxTextButton>(uiAccent);
    footerTab->setName("info");
    footerTab->setTablerIcon("at");
    footerTab->onClick = [this]
    {
        showInfoPrompt(shell_setup_support::getMixolveInfoMarkdown());
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*footerTab);

    startTimerHz(60);
    registerParameterListeners();

    ensureModuleTitle();
    updateSectionStates();
    setResizeLimits(minimumEditorWidth, minimumEditorHeight, maximumEditorWidth, maximumEditorHeight);

    const auto restoredEditorSize = getRestoredEditorSize();
    setResizeLimits(minimumEditorWidth, minimumEditorHeight, maximumEditorWidth, maximumEditorHeight);
    setSize(restoredEditorSize.x, restoredEditorSize.y);
    audioProcessor.setLastEditorSize(restoredEditorSize.x, restoredEditorSize.y);

    restoreEditorUiState();
    restoringEditorUiState = false;
    suppressEditorSizeStateSave = false;

    audioProcessor.getStateInformationForABCompareSnapshot(committedHistorySnapshot);

    if (! audioProcessor.isABCompareSnapshotValid(0))
        audioProcessor.setABCompareSnapshot(0, committedHistorySnapshot);

    if (! audioProcessor.isABCompareSnapshotValid(1))
        audioProcessor.setABCompareSnapshot(1, makeNoModuleSnapshotFrom(committedHistorySnapshot));

    if (! audioProcessor.isABCompareSnapshotValid(audioProcessor.getABCompareActiveSlot()))
        audioProcessor.setABCompareActiveSlot(0);

    updateUndoRedoButtons();
    if (routingInstanceId == 0)
        openRoutingInstance(restoredInstanceId);
}
