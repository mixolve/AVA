#include "OscPanel.h"
#include "Style.h"
#include "../crossover/UiSupport.h"

#include <utility>


OscPanel::OscPanel(Actions actionsIn) : actions(std::move(actionsIn))
{
    getProperties().set("emptyEditorBackground",true);
    enableControl = std::make_unique<LocalChoiceControl>("OSC", juce::StringArray { "OFF", "ON" }, 0);
    enableControl->setBottomIndicatorsVisible(false, false);
    enableControl->onValueChanged = [this]
    {
        auto settings = actions.getSettings();
        settings.enabled = enableControl->getSelectedChoiceIndex() == 1;
        actions.setSettings(settings);
        refresh();
    };
    addAndMakeVisible(*enableControl);

    statusLabel = std::make_unique<SquareTextLabel>();
    statusLabel->setFont(makeUiFont());
    statusLabel->setColour(juce::Label::textColourId, uiWhite);
    statusLabel->setColour(juce::Label::outlineColourId, uiGreyLight);
    statusLabel->setBorderSize(juce::BorderSize<int> { 1 });
    statusLabel->setJustificationType(juce::Justification::centred);
    statusLabel->setName("status");
    addAndMakeVisible(*statusLabel);

    const auto makePortControl = [this](const juce::String& name, bool input, int defaultPort)
    {
        auto control = std::make_unique<TextControl>(name, juce::String(defaultPort));
        control->setBottomIndicatorsVisible(false, false);
        control->onValueChanged = [this, input](const juce::String& text)
        {
            const auto digits = text.trim();
            if (digits.isEmpty() || digits.length() > 5 || !digits.containsOnly("0123456789")) return false;
            const auto port = digits.getIntValue();
            if (port < 1 || port > 65535) return false;
            auto settings = actions.getSettings();
            if (input) settings.inputPort = port;
            else settings.outputPort = port;
            return actions.setSettings(settings);
        };
        return control;
    };
    inputPortControl = makePortControl("IN-PORT", true, OscSettings::defaultInputPort);
    outputPortControl = makePortControl("OUT-PORT", false, OscSettings::defaultOutputPort);
    addAndMakeVisible(*inputPortControl);
    addAndMakeVisible(*outputPortControl);

    outputHostControl = std::make_unique<TextControl>("HOST", OscSettings().outputHost);
    outputHostControl->setFittedText();
    outputHostControl->setBottomIndicatorsVisible(false, false);
    outputHostControl->onValueChanged = [this](const juce::String& text)
    {
        const auto host = text.trim();
        if (host.isEmpty() || host.length() > 253) return false;
        auto settings = actions.getSettings();
        settings.outputHost = host;
        return actions.setSettings(settings);
    };
    addAndMakeVisible(*outputHostControl);
    refresh();
}

int OscPanel::getPreferredHeight() const noexcept
{
    if (isBlockCollapsed("OSC")) return controlBlockHeaderHeight;
    return measureControlGrid(*this, getLocalBounds().reduced(controlBlockPadding + controlBlockFrameThickness), {
        {enableControl.get()}, {inputPortControl.get()}, {outputPortControl.get()},
        {statusLabel.get()}, {outputHostControl.get(), 2} }).getHeight() + 2 * (controlBlockPadding + controlBlockFrameThickness) + controlBlockHeaderHeight;
}

void OscPanel::resized()
{
    const auto collapsed = isBlockCollapsed("OSC");
    for (auto* control : std::array<juce::Component*, 5> {
            enableControl.get(), inputPortControl.get(), outputPortControl.get(),
            statusLabel.get(), outputHostControl.get() })
    {
        control->setVisible(!collapsed);
        if (collapsed) control->setBounds({});
    }
    if (collapsed)
    {
        controlBlockFrames = {{getLocalBounds(), "OSC", uiGreyDark}};
        return;
    }
    layoutControlGrid(*this, getLocalBounds().reduced(controlBlockPadding + controlBlockFrameThickness).withTrimmedTop(controlBlockHeaderHeight), {
        {enableControl.get()}, {inputPortControl.get()}, {outputPortControl.get()},
        {statusLabel.get()}, {outputHostControl.get(), 2} });
    controlBlockFrames = {{getLocalBounds(), "OSC", uiGreyDark}};
}

void OscPanel::refresh()
{
    const auto settings = actions.getSettings();
    enableControl->setSelectedChoiceIndex(settings.enabled ? 1 : 0, false);
    statusLabel->setText(actions.isInputPortBusy() ? "BUSY" : "FREE", juce::dontSendNotification);
    inputPortControl->setValue(juce::String(settings.inputPort));
    outputPortControl->setValue(juce::String(settings.outputPort));
    outputHostControl->setValue(settings.outputHost);
}
