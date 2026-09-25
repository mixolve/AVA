#include "Editor.h"
#include "LookAndFeel.h"
#include "FilterSection.h"
#include "PresetSections.h"
#include "../crossover/Component.h"
#include "../routing/Panel.h"
#include "OscPanel.h"

#include <cmath>

namespace
{
bool usesLogFocusedParameterScale(const juce::Slider& slider) noexcept
{
    const auto minimum = static_cast<double>(slider.getMinimum());
    const auto maximum = static_cast<double>(slider.getMaximum());

    return minimum > 0.0 && maximum / minimum >= 100.0;
}

double sliderValueToFocusedParameterValue(const juce::Slider& slider) noexcept
{
    const auto currentValue = static_cast<double>(slider.getValue());

    if (usesLogFocusedParameterScale(slider))
    {
        const auto minimum = static_cast<double>(slider.getMinimum());
        const auto maximum = static_cast<double>(slider.getMaximum());
        return juce::jlimit(0.0,
                            1.0,
                            std::log(currentValue / minimum) / std::log(maximum / minimum));
    }

    return slider.getNormalisableRange().convertTo0to1(currentValue);
}

double focusedParameterValueToSliderValue(const juce::Slider& slider, const double focusedValue) noexcept
{
    const auto normalisedValue = juce::jlimit(0.0, 1.0, focusedValue);

    if (usesLogFocusedParameterScale(slider))
    {
        const auto minimum = static_cast<double>(slider.getMinimum());
        const auto maximum = static_cast<double>(slider.getMaximum());
        return minimum * std::pow(maximum / minimum, normalisedValue);
    }

    return slider.getNormalisableRange().convertFrom0to1(normalisedValue);
}
}

AvaAudioProcessorEditor::~AvaAudioProcessorEditor()
{
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
    if (activeInstanceId != 0
        && audioProcessor.getRoutingInstanceHandle(activeInstanceId) == nullptr)
        closeRoutingInstance();

    commitPendingHistorySnapshot();
    syncFocusedParameterControl();

    const auto activeModule = audioProcessor.getActiveModule();
    const auto moduleStateChanged = eqlModuleLoaded != (activeModule == AvaAudioProcessor::ActiveModule::eql)
        || fftModuleLoaded != (activeModule == AvaAudioProcessor::ActiveModule::fft)
        || tlsModuleLoaded != (activeModule == AvaAudioProcessor::ActiveModule::tls)
        || dynModuleLoaded != (activeModule == AvaAudioProcessor::ActiveModule::dyn)
        || trsModuleLoaded != (activeModule == AvaAudioProcessor::ActiveModule::trs);

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
            clipButton->setTextColourOverride(uiGreyLight);
        else
            clipButton->clearTextColourOverride();
    }

    if (auto* tlsEditor = dynamic_cast<CrossoverModuleComponent*>(tlsModuleEditor.get()))
        tlsEditor->refreshExternalState();

    if (auto* dynEditor = dynamic_cast<CrossoverModuleComponent*>(dynModuleEditor.get()))
        dynEditor->refreshExternalState();

    if (auto* trsEditor = dynamic_cast<CrossoverModuleComponent*>(trsModuleEditor.get()))
        trsEditor->refreshExternalState();

    if (auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
        editor->refreshExternalState();

    if (oscPanel != nullptr)
        oscPanel->refresh();

    refreshFftAnalyserResponse();
}

double AvaAudioProcessorEditor::getFocusedParameterControlValueForTarget() const noexcept
{
    if (focusedParameterTargetSlider == nullptr)
        return 0.0;

    return sliderValueToFocusedParameterValue(*focusedParameterTargetSlider);
}

double AvaAudioProcessorEditor::getFocusedParameterTargetValueForControl() const noexcept
{
    if (focusedParameterControl == nullptr || focusedParameterTargetSlider == nullptr)
        return 0.0;

    return focusedParameterValueToSliderValue(*focusedParameterTargetSlider, focusedParameterControl->getValue());
}

void AvaAudioProcessorEditor::syncFocusedParameterControl()
{
    if (focusedParameterControl == nullptr)
        return;

    shell_parameter_focus::clearFocusIfNotShowing(*this);

    auto* nextTarget = shell_parameter_focus::getFocusedValueSlider(*this);

    if (nextTarget != focusedParameterTargetSlider)
    {
        const auto preservedFilterScrollY = filterViewport.getViewPositionY();
        focusedParameterTargetSlider = nextTarget;

        if (focusedParameterTargetSlider != nullptr)
        {
            const juce::ScopedValueSetter<bool> scopedIgnore(suppressFocusedParameterControlChangeHandlers, true);
            focusedParameterControl->setValue(getFocusedParameterControlValueForTarget(),
                                              juce::dontSendNotification);
            focusedParameterControl->setEnabled(true);
            focusedParameterControl->setColour(juce::Slider::backgroundColourId, uiGrey800);
            focusedParameterControl->setColour(juce::Slider::trackColourId, uiWhite);
            focusedParameterControl->setAlpha(1.0f);
        }
        else
        {
            focusedParameterControl->setEnabled(false);
            focusedParameterControl->setColour(juce::Slider::backgroundColourId, uiGrey800);
            focusedParameterControl->setColour(juce::Slider::trackColourId, uiGrey500);
            focusedParameterControl->setAlpha(1.0f);
        }

        resized();

        const auto filterMaxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
        filterViewport.setViewPosition(0, juce::jlimit(0, filterMaxOffset, preservedFilterScrollY));

    }

    if (focusedParameterTargetSlider == nullptr || focusedParameterControl->isMouseButtonDown())
        return;

    const auto targetValue = getFocusedParameterControlValueForTarget();

    if (std::abs(focusedParameterControl->getValue() - targetValue) > 1.0e-6)
    {
        const juce::ScopedValueSetter<bool> scopedIgnore(suppressFocusedParameterControlChangeHandlers, true);
        focusedParameterControl->setValue(targetValue, juce::dontSendNotification);
    }
}

void AvaAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    shell_parameter_focus::clearFocus(*this);
    clearKeyboardFocus(*this);
}

void AvaAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (hostParametersViewport.getBounds().contains(event.getPosition()))
    {
        if (scrollViewportWithWheel(hostParametersViewport, hostParametersContent.getHeight(), wheel, event.mods.isShiftDown()))
            return;
    }

    if (! filterViewport.getBounds().contains(event.getPosition()))
        return;

    scrollViewportWithWheel(filterViewport, getActiveFilterContentHeight(), wheel, event.mods.isShiftDown());
}
