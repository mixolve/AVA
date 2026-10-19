#include "Editor.h"
#include "Page.h"
#include "UiSupport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/LocalParameterControl.h"
#include "../shell/ParameterControl.h"

#include <utility>

using namespace crossover_ui;

class CrossoverSettingsPage final : public CrossoverModulePage
{
public:
    explicit CrossoverSettingsPage(CrossoverModuleComponent& ownerIn)
        : owner(ownerIn)
    {
        if (! owner.config.showCrossoverControls)
            return;

        if (owner.config.processingInputParameterId.isNotEmpty())
        {
            processingInputControl = std::make_unique<ChoiceControl>(owner.valueTreeState,
                owner.config.processingInputParameterId, "CH-IN");
            processingInputControl->onValueChanged = [this]
            {
                if (owner.config.markParametersDirty) owner.config.markParametersDirty();
                owner.clearFocus();
            };
            addAndMakeVisible(*processingInputControl);
        }

        const auto makeSoloButton = [this] (const juce::String& id, const char* label,
            std::unique_ptr<BoxTextButton>& button,
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& attachment)
        {
            if (id.isEmpty()) return;
            button = makeTextButton(label);
            button->setClickingTogglesState(true);
            button->getProperties().set(juce::Identifier("oscParameterId"), id);
            auto* target = button.get();
            button->setLongPressPromptActions({}, [this, id, label, target]
            {
                owner.assignButtonToHostSlot(id, label, target);
            });
            button->onClick = [this]
            {
                if (owner.config.markParametersDirty) owner.config.markParametersDirty();
                owner.clearFocus();
            };
            addAndMakeVisible(*button);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                owner.valueTreeState, id, *button);
        };
        makeSoloButton(owner.config.soloInstanceParameterId, "SOLO-INS", soloInstanceButton, soloInstanceAttachment);
        makeSoloButton(owner.config.soloChannelParameterId, "SOLO-CH", soloChannelButton, soloChannelAttachment);

        autoSoloButton = makeTextButton("AUTO-SOLO");
        if (owner.config.makeCrossoverParameterId != nullptr)
            autoSoloButton->getProperties().set(juce::Identifier("oscParameterId"),
                                                owner.config.makeCrossoverParameterId("auto-solo"));
        autoSoloButton->setClickingTogglesState(true);
        autoSoloButton->setToggleState(owner.autoSoloEnabled, juce::dontSendNotification);
        autoSoloButton->setLongPressPromptActions({}, [this]
        {
            if (owner.config.makeCrossoverParameterId != nullptr)
                owner.assignButtonToHostSlot(owner.config.makeCrossoverParameterId("auto-solo"),
                                             "AUTO-SOLO",
                                             autoSoloButton.get());
        });
        autoSoloButton->onClick = [this]
        {
            if (! isAutoSoloAvailable())
            {
                refreshAutoSoloButtonState();
                return;
            }

            owner.setAutoSoloEnabled(autoSoloButton->getToggleState());
            owner.clearFocus();
        };
        addAndMakeVisible(*autoSoloButton);

        soloModeControl = std::make_unique<LocalChoiceControl>("SOLO-MODE",
                                                                juce::StringArray { "EXCLUSIVE", "INCLUSIVE" },
                                                                0);
        soloModeControl->onValueChanged = [this]
        {
            owner.setManualSoloInclusive(soloModeControl->getSelectedChoiceIndex() == 1);
            owner.clearFocus();
        };
        addAndMakeVisible(*soloModeControl);

        addCrossoverButton = makeTextButton("XOV-ADD");
        addCrossoverButton->getProperties().set(juce::Identifier("oscParameterId"),
                                                "xov-add");
        addCrossoverButton->onClick = [this]
        {
            owner.changeActiveSplitCount(1);
            owner.clearFocus();
        };
        addAndMakeVisible(*addCrossoverButton);

        removeCrossoverButton = makeTextButton("XOV-DEL");
        removeCrossoverButton->getProperties().set(juce::Identifier("oscParameterId"),
                                                   "xov-del");
        removeCrossoverButton->onClick = [this]
        {
            owner.changeActiveSplitCount(-1);
            owner.clearFocus();
        };
        addAndMakeVisible(*removeCrossoverButton);

        for (size_t index = 0; index < splitControls.size(); ++index)
        {
            auto control = std::make_unique<ParameterControl>(
                owner.valueTreeState,
                owner.config.makeCrossoverParameterId(splitParameterSuffixes[index]),
                splitLabels[index],
                owner.config.crossoverDecimals);
            control->onValueChanged = [this, index]
            {
                owner.constrainSplitFrequency(index);
            };
            addAndMakeVisible(*control);
            splitControls[index] = std::move(control);
        }

        refreshExternalState();

    }

    int getPreferredHeight() const override
    {
        const auto height = getFlatPreferredHeight();
        return height;
    }

    int getFlatPreferredHeight() const
    {
        if (! owner.config.showCrossoverControls)
            return 0;

        auto height = 0;

        if (processingInputControl != nullptr)
            height += processingInputControl->getPreferredHeight() + verticalGap * 2;

        if (soloInstanceButton != nullptr || soloChannelButton != nullptr)
            height += rowHeight + verticalGap;

        height += rowHeight;

        for (const auto& control : splitControls)
        {
            if (control != nullptr)
                height += verticalGap + control->getPreferredHeight();
        }

        height += verticalGap + rowHeight;

        if (owner.config.showAutoSolo)
            height += verticalGap + rowHeight;

        height += verticalGap + rowHeight;

        return height + moduleContentBottomGap;
    }

    void refreshExternalState() override
    {
        if (! owner.config.showCrossoverControls)
            return;

        if (processingInputControl != nullptr && owner.config.isProcessingInputAvailable)
            if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(
                    owner.valueTreeState.getParameter(owner.config.processingInputParameterId)))
                for (int choice = 0; choice < parameter->choices.size(); ++choice)
                    processingInputControl->setChoiceEnabled(choice, owner.config.isProcessingInputAvailable(choice));

        const auto activeSplitCount = owner.getActiveSplitCount();
        const auto canAdd = activeSplitCount < splitControls.size();
        const auto canRemove = activeSplitCount > 0;

        if (addCrossoverButton != nullptr)
        {
            addCrossoverButton->setEnabled(canAdd);
            addCrossoverButton->setAlpha(1.0f);
        }

        if (removeCrossoverButton != nullptr)
        {
            removeCrossoverButton->setEnabled(canRemove);
            removeCrossoverButton->setAlpha(1.0f);
        }

        refreshAutoSoloButtonState();
        refreshSoloModeButtonState();

        for (size_t index = 0; index < splitControls.size(); ++index)
        {
            if (auto* control = splitControls[index].get())
            {
                const auto enabled = index < activeSplitCount;
                control->setEnabled(true);
                control->setAlpha(1.0f);
                control->setInteractionEnabled(enabled);

                if (enabled)
                    control->clearOverrideText();
                else
                    control->setOverrideText("OFF");
            }
        }
    }

    void resized() override
    {
        if (! owner.config.showCrossoverControls)
            return;

        auto bounds = getLocalBounds().withHeight(juce::jmax(getHeight(), getFlatPreferredHeight()));

        auto placeButton = [&bounds] (BoxTextButton* button)
        {
            if (button == nullptr)
                return;

            button->setBounds(bounds.removeFromTop(rowHeight));

            if (! bounds.isEmpty())
                bounds.removeFromTop(verticalGap);
        };

        auto placeControl = [&bounds] (auto* control)
        {
            if (control == nullptr)
                return;

            control->setBounds(bounds.removeFromTop(control->getPreferredHeight()));

            if (! bounds.isEmpty())
                bounds.removeFromTop(verticalGap);
        };

        if (processingInputControl != nullptr)
        {
            placeControl(processingInputControl.get());
        }
        if (soloInstanceButton != nullptr && soloChannelButton != nullptr)
        {
            auto row = bounds.removeFromTop(rowHeight);
            const auto width = (row.getWidth() - verticalGap) / 2;
            soloInstanceButton->setBounds(row.removeFromLeft(width));
            row.removeFromLeft(verticalGap);
            soloChannelButton->setBounds(row);
            bounds.removeFromTop(verticalGap);
        }
        if (processingInputControl != nullptr) bounds.removeFromTop(verticalGap);
        placeButton(addCrossoverButton.get());

        for (auto& control : splitControls)
            placeControl(control.get());

        placeButton(removeCrossoverButton.get());

        if (owner.config.showAutoSolo)
            placeButton(autoSoloButton.get());
        else if (autoSoloButton != nullptr)
            autoSoloButton->setBounds({});

        placeControl(soloModeControl.get());

        refreshExternalState();
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        owner.clearFocus();
    }

private:
    bool isAutoSoloAvailable() const
    {
        return owner.getActiveSplitCount() > 0;
    }

    void refreshAutoSoloButtonState()
    {
        if (autoSoloButton == nullptr)
            return;

        autoSoloButton->setVisible(owner.config.showAutoSolo);
        autoSoloButton->setToggleState(owner.autoSoloEnabled, juce::dontSendNotification);

        const auto available = owner.config.showAutoSolo && isAutoSoloAvailable();
        autoSoloButton->setEnabled(available);
        autoSoloButton->setAlpha(1.0f);
    }

    void refreshSoloModeButtonState()
    {
        if (soloModeControl == nullptr)
            return;

        soloModeControl->setSelectedChoiceIndex(owner.manualSoloInclusive ? 1 : 0, false);
        const auto available = ! owner.autoSoloEnabled && owner.getActiveSplitCount() > 0;
        soloModeControl->setInteractionEnabled(available);
        soloModeControl->setAlpha(1.0f);
    }

    CrossoverModuleComponent& owner;
    std::unique_ptr<ChoiceControl> processingInputControl;
    std::unique_ptr<BoxTextButton> soloInstanceButton, soloChannelButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloInstanceAttachment, soloChannelAttachment;
    std::unique_ptr<BoxTextButton> autoSoloButton;
    std::unique_ptr<LocalChoiceControl> soloModeControl;
    std::unique_ptr<BoxTextButton> addCrossoverButton;
    std::unique_ptr<BoxTextButton> removeCrossoverButton;
    std::array<std::unique_ptr<ParameterControl>, splitControlCount> splitControls;

};

std::unique_ptr<CrossoverModulePage> makeCrossoverSettingsPage(CrossoverModuleComponent& owner)
{
    return std::make_unique<CrossoverSettingsPage>(owner);
}
