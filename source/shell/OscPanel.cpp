#include "OscPanel.h"
#include "Style.h"
#include "../crossover/UiSupport.h"

#include <utility>

using crossover_ui::makeTextButton;

OscPanel::OscPanel(Actions actionsIn) : actions(std::move(actionsIn))
{
    setOpaque(true);

    enableButton = makeTextButton("OSC-ENABLE");
    enableButton->setClickingTogglesState(true);
    enableButton->onClick = [this]
    {
        auto settings = actions.getSettings();
        settings.enabled = enableButton->getToggleState();
        actions.setSettings(settings);
        refresh();
        actions.clearFocus();
    };
    addAndMakeVisible(*enableButton);

    listButton = makeTextButton("LIST");
    listButton->setClickingTogglesState(true);
    listButton->onClick = [this]
    {
        actions.toggleParameterList();
        refresh();
        actions.clearFocus();
    };
    addAndMakeVisible(*listButton);

    statusLabel = std::make_unique<juce::Label>();
    statusLabel->setFont(makeUiFont());
    statusLabel->setColour(juce::Label::textColourId, uiWhite);
    statusLabel->setColour(juce::Label::outlineColourId, uiGrey500);
    statusLabel->setBorderSize(juce::BorderSize<int> { 1 });
    statusLabel->setJustificationType(juce::Justification::centred);
    statusLabel->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*statusLabel);

    inputPortLabel = makeLabel("IN-PORT");
    inputPortButton = makeTextButton({});
    inputPortButton->onClick = [this] { editPort(true); };
    addAndMakeVisible(*inputPortButton);

    outputPortLabel = makeLabel("OUT-PORT");
    outputPortButton = makeTextButton({});
    outputPortButton->onClick = [this] { editPort(false); };
    addAndMakeVisible(*outputPortButton);

    outputHostLabel = makeLabel("HOST");
    outputHostButton = makeTextButton({});
    outputHostButton->onClick = [this]
    {
        const auto settings = actions.getSettings();
        actions.showTextPrompt(*outputHostButton, settings.outputHost,
                               [this] (const juce::String& text)
                               {
                                   const auto host = text.trim();
                                   if (host.isEmpty() || host.length() > 253)
                                       return false;
                                   auto updated = actions.getSettings();
                                   updated.outputHost = host;
                                   if (! actions.setSettings(updated))
                                       return false;
                                   refresh();
                                   actions.clearFocus();
                                   return true;
                               });
    };
    addAndMakeVisible(*outputHostButton);
    refresh();
}

std::unique_ptr<BoxTextButton> OscPanel::makeLabel(const juce::String& text)
{
    auto label = makeTextButton(text, uiGrey500);
    label->setClickingTogglesState(false);
    label->setPressFillEnabled(false);
    label->setTextJustification(juce::Justification::centredLeft);
    label->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*label);
    return label;
}

void OscPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(uiBlack);
}

void OscPanel::resized()
{
    auto bounds = getLocalBounds();
    auto controls = bounds.removeFromTop(rowHeight);
    const auto enableWidth = juce::jmax((controls.getWidth() - parameterGap * 2) / 3,
                                       getTextPixelWidth(makeUiFont(), "OSC-ENABLED") + uiGapDouble);
    enableButton->setBounds(controls.removeFromLeft(enableWidth));
    controls.removeFromLeft(parameterGap);
    listButton->setBounds(controls.removeFromLeft((controls.getWidth() - parameterGap) / 2));
    controls.removeFromLeft(parameterGap);
    statusLabel->setBounds(controls);
    bounds.removeFromTop(verticalGap);

    const auto placePair = [&bounds] (BoxTextButton& label, BoxTextButton& value)
    {
        auto row = bounds.removeFromTop(rowHeight);
        const auto labelWidth = (row.getWidth() - parameterGap) / 2;
        label.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(parameterGap);
        value.setBounds(row);
        bounds.removeFromTop(verticalGap);
    };
    placePair(*inputPortLabel, *inputPortButton);
    placePair(*outputPortLabel, *outputPortButton);
    placePair(*outputHostLabel, *outputHostButton);
}

void OscPanel::editPort(const bool inputPort)
{
    auto& button = inputPort ? *inputPortButton : *outputPortButton;
    const auto settings = actions.getSettings();
    const auto currentPort = inputPort ? settings.inputPort : settings.outputPort;
    actions.showTextPrompt(button, juce::String(currentPort),
                           [this, inputPort] (const juce::String& text)
                           {
                               const auto trimmed = text.trim();
                               if (trimmed.isEmpty() || ! trimmed.containsOnly("0123456789"))
                                   return false;
                               const auto port = trimmed.getIntValue();
                               if (port < 1 || port > 65535)
                                   return false;

                               auto updated = actions.getSettings();
                               if (inputPort)
                                   updated.inputPort = port;
                               else
                                   updated.outputPort = port;

                               if (! actions.setSettings(updated))
                               {
                                   if (! inputPort || ! updated.enabled)
                                       return false;
                                   updated.enabled = false;
                                   if (! actions.setSettings(updated))
                                       return false;
                               }
                               refresh();
                               actions.clearFocus();
                               return true;
                           });
}

void OscPanel::refresh()
{
    const auto settings = actions.getSettings();
    enableButton->setToggleState(settings.enabled, juce::dontSendNotification);
    enableButton->setButtonText(settings.enabled ? "OSC-ENABLED" : "OSC-ENABLE");
    listButton->setToggleState(actions.isParameterListVisible(), juce::dontSendNotification);
    statusLabel->setText(actions.isInputPortBusy() ? "BUSY" : "FREE", juce::dontSendNotification);
    inputPortButton->setButtonText(juce::String(settings.inputPort));
    outputPortButton->setButtonText(juce::String(settings.outputPort));
    outputHostButton->setButtonText(settings.outputHost);
}
