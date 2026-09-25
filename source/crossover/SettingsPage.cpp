#include "Component.h"
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
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    explicit CrossoverSettingsPage(CrossoverModuleComponent& ownerIn)
        : owner(ownerIn)
    {
        if (! owner.config.showCrossoverControls)
            return;

        if (owner.config.crossoverSettingsHeading.isNotEmpty())
        {
            settingsHeading = makeTextButton(owner.config.crossoverSettingsHeading, uiGrey500);
            settingsHeading->setFillVisible(false);
            settingsHeading->setAlwaysAccentOutline(false);
            settingsHeading->setToggleAccentVisible(false);
            settingsHeading->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(*settingsHeading);
        }

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

        globalListenHeading = makeTextButton("LISTEN", uiAccent);
        globalListenHeading->setClickingTogglesState(false);
        globalListenHeading->setBorderVisible(true);
        globalListenHeading->setFillVisible(false);
        globalListenHeading->setDividerLineVisible(false);
        globalListenHeading->setPressFillEnabled(false);
        globalListenHeading->setTextJustification(juce::Justification::centredLeft);
        globalListenHeading->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*globalListenHeading);

        for (size_t index = 0; index < globalListenButtons.size(); ++index)
        {
            auto button = makeTextButton(globalListenLabels[index]);
            button->setClickingTogglesState(true);
            const auto parameterId = owner.config.makeCrossoverParameterId(globalListenSuffixes[index]);
            button->getProperties().set(juce::Identifier("oscParameterId"), parameterId);
            globalListenAttachments[index] = std::make_unique<ButtonAttachment>(owner.valueTreeState,
                                                                                   parameterId,
                                                                                   *button);
            button->setLongPressPromptActions({}, [this, index, parameterId]
            {
                owner.assignButtonToHostSlot(parameterId, globalListenLabels[index], nullptr);
            });
            button->onClick = [this, index]
            {
                if (globalListenButtons[index] != nullptr && globalListenButtons[index]->getToggleState())
                {
                    for (size_t otherIndex = 0; otherIndex < globalListenButtons.size(); ++otherIndex)
                    {
                        if (otherIndex != index)
                            owner.setParameterPlainValue(owner.config.makeCrossoverParameterId(globalListenSuffixes[otherIndex]),
                                                         0.0f);
                    }
                }

                owner.clearFocus();
            };
            addAndMakeVisible(*button);
            globalListenButtons[index] = std::move(button);
        }

        globalListenInactive = makeTextButton("MM");
        globalListenInactive->setEnabled(false);
        globalListenInactive->setClickingTogglesState(false);
        globalListenInactive->setPressFillEnabled(false);
        globalListenInactive->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*globalListenInactive);

        refreshExternalState();
    }

    int getPreferredHeight() const override
    {
        if (! owner.config.showCrossoverControls)
            return 0;

        auto height = 0;

        if (settingsHeading != nullptr)
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
        height += verticalGap * 2 + rowHeight * 3;

        return height + moduleContentBottomGap;
    }

    void refreshExternalState() override
    {
        if (! owner.config.showCrossoverControls)
            return;

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

        auto bounds = getLocalBounds();

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

        placeButton(settingsHeading.get());
        placeButton(addCrossoverButton.get());

        for (auto& control : splitControls)
            placeControl(control.get());

        placeButton(removeCrossoverButton.get());

        if (owner.config.showAutoSolo)
            placeButton(autoSoloButton.get());
        else if (autoSoloButton != nullptr)
            autoSoloButton->setBounds({});

        placeControl(soloModeControl.get());

        placeButton(globalListenHeading.get());

        const auto placeListenRow = [&bounds, this] (const std::array<int, 4>& buttonOrder)
        {
            auto rowBounds = bounds.removeFromTop(rowHeight);
            const auto columnWidth = (rowBounds.getWidth() - parameterGap * 3) / 4;

            for (size_t column = 0; column < 4; ++column)
            {
                const auto isLast = column == 3;
                auto cellBounds = rowBounds.removeFromLeft(isLast ? rowBounds.getWidth() : columnWidth);
                const auto buttonIndex = buttonOrder[column];

                if (buttonIndex >= 0 && static_cast<size_t>(buttonIndex) < globalListenButtons.size())
                    globalListenButtons[static_cast<size_t>(buttonIndex)]->setBounds(cellBounds);
                else if (globalListenInactive != nullptr)
                    globalListenInactive->setBounds(cellBounds);

                if (! isLast)
                    rowBounds.removeFromLeft(juce::jmin(parameterGap, rowBounds.getWidth()));
            }

            if (! bounds.isEmpty())
                bounds.removeFromTop(verticalGap);
        };

        placeListenRow({ 0, 1, 2, 3 });
        placeListenRow({ 4, 5, -1, 6 });

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
    std::unique_ptr<BoxTextButton> settingsHeading;
    std::unique_ptr<BoxTextButton> autoSoloButton;
    std::unique_ptr<LocalChoiceControl> soloModeControl;
    std::unique_ptr<BoxTextButton> addCrossoverButton;
    std::unique_ptr<BoxTextButton> removeCrossoverButton;
    std::array<std::unique_ptr<ParameterControl>, splitControlCount> splitControls;
    std::unique_ptr<BoxTextButton> globalListenHeading;
    std::array<std::unique_ptr<BoxTextButton>, globalListenSuffixes.size()> globalListenButtons;
    std::array<std::unique_ptr<ButtonAttachment>, globalListenSuffixes.size()> globalListenAttachments;
    std::unique_ptr<BoxTextButton> globalListenInactive;

};

std::unique_ptr<CrossoverModulePage> makeCrossoverSettingsPage(CrossoverModuleComponent& owner)
{
    return std::make_unique<CrossoverSettingsPage>(owner);
}
