#include "Editor.h"
#include "EdgeResizeHandle.h"
#include "LookAndFeel.h"
#include "FilterSection.h"
#include "PresetSections.h"
#include "SetupSupport.h"
#include "../routing/Panel.h"
#include "../routing/State.h"
#include "OscPanel.h"
#include "../modules/fft/Processor.h"
#include "../modules/eql/Processor.h"

#include <algorithm>

namespace
{
constexpr double fineControlScale = 0.1;

double getFineControlScale(const bool fineControl) noexcept
{
    return fineControl ? fineControlScale : 1.0;
}

bool applyWheelToSliderValue(juce::Slider& slider, const juce::MouseWheelDetails& wheel, const bool fineControl)
{
    const auto dominantDelta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY
                                                                                : -wheel.deltaX;

    if (std::abs(dominantDelta) < 1.0e-6f)
        return false;

    const auto directionalDelta = wheel.isReversed ? -dominantDelta : dominantDelta;
    const auto direction = directionalDelta < 0.0f ? -1.0 : 1.0;
    const auto fineScale = getFineControlScale(fineControl);
    const auto minimum = static_cast<double>(slider.getMinimum());
    const auto maximum = static_cast<double>(slider.getMaximum());
    const auto currentValue = static_cast<double>(slider.getValue());

    if (minimum > 0.0 && maximum / minimum >= 100.0)
    {
        const auto smoothOctaves = static_cast<double>(directionalDelta) * 0.75 * fineScale;
        const auto minimumSmoothOctaves = (1.0 / 96.0) * fineScale;
        const auto octaveDelta = wheel.isSmooth
            ? direction * juce::jmax(std::abs(smoothOctaves), minimumSmoothOctaves)
            : (direction / 12.0) * fineScale;
        const auto clampedOctaveDelta = juce::jlimit(-0.25, 0.25, octaveDelta);
        const auto nextValue = currentValue * std::pow(2.0, clampedOctaveDelta);

        slider.setValue(juce::jlimit(minimum, maximum, nextValue), juce::sendNotificationSync);
        return true;
    }

    const auto& range = slider.getNormalisableRange();
    const auto currentNormalised = static_cast<double>(range.convertTo0to1(currentValue));
    const auto smoothStep = static_cast<double>(directionalDelta) * 0.025 * fineScale;
    const auto minimumSmoothStep = 0.0025 * fineScale;
    const auto normalisedStep = wheel.isSmooth
        ? direction * juce::jmax(std::abs(smoothStep), minimumSmoothStep)
        : direction * 0.025 * fineScale;
    const auto nextNormalised = juce::jlimit(0.0, 1.0, currentNormalised + normalisedStep);

    slider.setValue(range.convertFrom0to1(nextNormalised), juce::sendNotificationSync);
    return true;
}

bool applyWheelToNormalisedSliderValue(juce::Slider& slider, const juce::MouseWheelDetails& wheel, const bool fineControl)
{
    const auto dominantDelta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY
                                                                                : -wheel.deltaX;

    if (std::abs(dominantDelta) < 1.0e-6f)
        return false;

    const auto directionalDelta = wheel.isReversed ? -dominantDelta : dominantDelta;
    const auto direction = directionalDelta < 0.0f ? -1.0 : 1.0;
    const auto fineScale = getFineControlScale(fineControl);
    const auto minimumSmoothStep = 0.005 * fineScale;
    const auto normalisedStep = wheel.isSmooth
        ? direction * juce::jmax(std::abs(static_cast<double>(directionalDelta) * 0.02 * fineScale), minimumSmoothStep)
        : direction * 0.025 * fineScale;
    const auto nextValue = juce::jlimit(0.0,
                                        1.0,
                                        static_cast<double>(slider.getValue()) + normalisedStep);

    if (std::abs(nextValue - static_cast<double>(slider.getValue())) <= 1.0e-9)
        return false;

    slider.setValue(nextValue, juce::sendNotificationSync);
    return true;
}

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

class FocusedParameterSlider final : public juce::Slider
{
public:
    FocusedParameterSlider()
        : juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds();
        const auto normalisedValue = static_cast<float>(juce::jlimit(0.0, 1.0, getValue()));
        const auto fillWidth = juce::roundToInt(static_cast<float>(bounds.getWidth()) * normalisedValue);

        graphics.setColour(findColour(juce::Slider::backgroundColourId));
        graphics.fillRect(bounds);

        if (isEnabled() && fillWidth > 0)
        {
            graphics.setColour(findColour(juce::Slider::trackColourId));
            graphics.fillRect(bounds.withWidth(fillWidth));
        }

        graphics.setColour(uiGrey500);
        graphics.drawRect(bounds, 1);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragValue = getValue();
        lastDragHorizontalDelta = 0.0;
        lastDragVerticalDelta = 0.0;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        const auto width = juce::jmax(1, getWidth());
        const auto horizontalDelta = static_cast<double>(event.getDistanceFromDragStartX());
        const auto verticalDelta = -static_cast<double>(event.getDistanceFromDragStartY());
        const auto stepHorizontalDelta = horizontalDelta - lastDragHorizontalDelta;
        const auto stepVerticalDelta = verticalDelta - lastDragVerticalDelta;
        const auto delta = ((stepHorizontalDelta + stepVerticalDelta) / static_cast<double>(width))
            * getFineControlScale(event.mods.isShiftDown());

        lastDragHorizontalDelta = horizontalDelta;
        lastDragVerticalDelta = verticalDelta;
        dragValue = juce::jlimit(0.0, 1.0, dragValue + delta);
        setValue(dragValue, juce::sendNotificationSync);
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (onWheel != nullptr && onWheel(event, wheel))
            return;

        applyWheelToSliderValue(*this, wheel, event.mods.isShiftDown());
    }

    std::function<bool(const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

private:
    double dragValue = 0.0;
    double lastDragHorizontalDelta = 0.0;
    double lastDragVerticalDelta = 0.0;
};

}

AvaAudioProcessorEditor::AvaAudioProcessorEditor(AvaAudioProcessor& processorToEdit,
                                                 const int routingInstanceIdIn)
    : AudioProcessorEditor(&processorToEdit),
      audioProcessor(processorToEdit),
      valueTreeState(processorToEdit.getValueTreeState()),
      lookAndFeel(std::make_unique<AvaLookAndFeel>()),
      routingInstanceId(routingInstanceIdIn)
{
    audioProcessor.setOscActionEditor(this);
    shell_parameter_focus::clearFocus(*this);

    setLookAndFeel(lookAndFeel.get());
    setOpaque(true);
    setResizable(true, false);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    hostParametersViewport.setViewedComponent(&hostParametersContent, false);
    hostParametersViewport.setScrollBarsShown(false, true);
    hostParametersViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    hostParametersViewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(hostParametersViewport);
    routingPanel = std::make_unique<RoutingPanel>(valueTreeState);
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
                       getLocalArea(&anchor, anchor.getLocalBounds()));
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
    filterViewport.setScrollBarsShown(false, false);
    filterViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    filterViewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(filterViewport);

    auto focusedSlider = std::make_unique<FocusedParameterSlider>();
    focusedSlider->onWheel = [this] (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        if (focusedParameterTargetSlider == nullptr)
            return false;

        return focusedParameterControl != nullptr
            && applyWheelToNormalisedSliderValue(*focusedParameterControl, wheel, event.mods.isShiftDown());
    };
    focusedParameterControl = std::move(focusedSlider);
    focusedParameterControl->setSliderStyle(juce::Slider::LinearHorizontal);
    focusedParameterControl->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    focusedParameterControl->setSliderSnapsToMousePosition(false);
    focusedParameterControl->setVelocityBasedMode(false);
    focusedParameterControl->setScrollWheelEnabled(true);
    focusedParameterControl->setColour(juce::Slider::backgroundColourId, uiGrey800);
    focusedParameterControl->setColour(juce::Slider::trackColourId, uiWhite);
    focusedParameterControl->setColour(juce::Slider::thumbColourId, uiWhite);
    focusedParameterControl->setColour(juce::Slider::rotarySliderFillColourId, uiWhite);
    focusedParameterControl->setColour(juce::Slider::rotarySliderOutlineColourId, uiGrey500);
    focusedParameterControl->setRange(0.0, 1.0, 0.0);
    focusedParameterControl->setValue(0.5, juce::dontSendNotification);
    focusedParameterControl->setEnabled(false);
    focusedParameterControl->setAlpha(1.0f);
    focusedParameterControl->onValueChange = [this]
    {
        if (suppressFocusedParameterControlChangeHandlers || focusedParameterControl == nullptr || focusedParameterTargetSlider == nullptr)
            return;

        focusedParameterTargetSlider->setValue(getFocusedParameterTargetValueForControl(),
                                               juce::sendNotificationSync);

        const juce::ScopedValueSetter<bool> scopedIgnore(suppressFocusedParameterControlChangeHandlers, true);
        focusedParameterControl->setValue(getFocusedParameterControlValueForTarget(),
                                          juce::dontSendNotification);
    };
    addAndMakeVisible(*focusedParameterControl);

    verticalResizeHandle = std::make_unique<EdgeResizeHandle>(
        *this, EdgeResizeHandle::Axis::vertical, minimumEditorHeight, maximumEditorHeight, uiGrey500);
    addAndMakeVisible(*verticalResizeHandle);

    clipButton = std::make_unique<BoxTextButton>(uiClip);
    clipButton->setButtonText("C");
    clipButton->setTextJustification(juce::Justification::centred);
    clipButton->setClickingTogglesState(false);
    clipButton->setFillVisible(false);
    clipButton->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*clipButton);

    hostButton = std::make_unique<BoxTextButton>(uiAccent);
    hostButton->setButtonText({});
    hostButton->setTablerIcon("map-pin-share");
    hostButton->setTextJustification(juce::Justification::centred);
    hostButton->setClickingTogglesState(true);
    hostButton->setToggleAccentVisible(true);
    hostButton->onClick = [this]
    {
        toggleHostParametersSection();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*hostButton);

    moduleAddButton = std::make_unique<BoxTextButton>(uiClip);
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

    if (auto* fftProcessor = audioProcessor.getFftModuleProcessor())
    {
        auto& fftState = fftProcessor->getValueTreeState();

        setupFftControls(fftState, *fftProcessor);
        fftAnalyserComponent = shell_setup_support::createFftAnalyserComponent(*fftProcessor);
        addAndMakeVisible(*fftAnalyserComponent);
    }

    setupShellControls();

    setupPresetControls();

    filterDisplayOrder.reserve(EqlModuleProcessor::maxFilterCount);
    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
        filterDisplayOrder.push_back(filterIndex);

    if (auto* initialEqlProcessor = audioProcessor.getEqlModuleProcessor())
        setupEqlControls(initialEqlProcessor->getValueTreeState());

    restoreEditorStateFromValueTree();
    routingExpanded = ! audioProcessor.isRoutingInstance();
    hostParametersExpanded = false;
    oscExpanded = false;

    footerTab = std::make_unique<BoxTextButton>(uiAccent);
    footerTab->setIconOnlyText("I");
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

    suppressEditorSizeStateSave = false;
    syncFocusedParameterControl();
    refreshFftAnalyserResponse();

    audioProcessor.getStateInformationForABCompareSnapshot(committedHistorySnapshot);

    if (! audioProcessor.isABCompareSnapshotValid(0))
        audioProcessor.setABCompareSnapshot(0, committedHistorySnapshot);

    if (! audioProcessor.isABCompareSnapshotValid(1))
        audioProcessor.setABCompareSnapshot(1, makeNoModuleSnapshotFrom(committedHistorySnapshot));

    if (! audioProcessor.isABCompareSnapshotValid(audioProcessor.getABCompareActiveSlot()))
        audioProcessor.setABCompareActiveSlot(0);

    updateUndoRedoButtons();
}
