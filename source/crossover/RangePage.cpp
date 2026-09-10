#include "ModuleComponent.h"
#include "Pages.h"
#include "UiSupport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/LocalParameterControl.h"
#include "../shell/ParameterControl.h"

#include <algorithm>
#include <utility>

using namespace crossover_ui;
using ControlKind = CrossoverModuleComponent::ControlKind;
using CrossoverControlSpec = CrossoverModuleComponent::CrossoverControlSpec;

class CrossoverRangePage final : public CrossoverModulePage,
                                                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    CrossoverRangePage(CrossoverModuleComponent& ownerIn,
                      const size_t rangeIndexIn,
                      juce::Colour accent)
        : owner(ownerIn),
          rangeIndex(rangeIndexIn),
          soloButton(accent),
          moduleHeading(uiGrey500)
    {
        soloButton.setButtonText("SOLO");
        soloButton.setTextJustification(juce::Justification::centred);
        soloButton.setClickingTogglesState(false);
        soloButton.setLongPressPromptActions({}, [this]
        {
            if (owner.config.makeCrossoverSoloParameterId != nullptr)
                owner.assignButtonToHostSlot(owner.config.makeCrossoverSoloParameterId(rangeIndex), "SOLO", &soloButton);
        });
        soloButton.onClick = [this]
        {
            owner.toggleManualSolo(rangeIndex);
            refreshSoloButtonState();
        };
        if (owner.config.showCrossoverSolo)
            addAndMakeVisible(soloButton);

        if (owner.config.showModuleHeading)
        {
            moduleHeading.setButtonText(owner.config.moduleKey.toUpperCase());
            moduleHeading.setTextJustification(juce::Justification::centred);
            moduleHeading.setAlwaysAccentOutline(false);
            moduleHeading.setToggleAccentVisible(false);
            moduleHeading.setLongPressAction([this]
            {
                if (owner.config.onModuleCloseRequest != nullptr)
                    owner.config.onModuleCloseRequest();
            }, 500, "CLOSE?");
            addAndMakeVisible(moduleHeading);
        }

        if (owner.config.showCrossoverSolo)
        {
            const auto soloParameterId = owner.config.makeCrossoverSoloParameterId(rangeIndex);
            listenedParameterIds.push_back(soloParameterId);
            owner.valueTreeState.addParameterListener(soloParameterId, this);
        }

        pinnedTail = std::make_unique<juce::Component>();
        addControlSpecs(owner.config.rangeControls, *this);
        tailRowStart = rows.size();
        addControlSpecs(owner.config.rangeTailControls, *pinnedTail);
        refreshSoloButtonState();
        updateTimeModeControls();
    }

    ~CrossoverRangePage() override
    {
        for (const auto& parameterId : listenedParameterIds)
            owner.valueTreeState.removeParameterListener(parameterId, this);
    }

    void refreshExternalState() override
    {
        const auto previousPreferredHeight = getPreferredHeight();
        const auto orderChanged = reorderRows("gain");
        refreshSoloButtonState();
        updateToggleLabels();
        updateTimeModeControls();

        if (orderChanged || previousPreferredHeight != getPreferredHeight())
            resized();
    }

    int getPreferredHeight() const override
    {
        auto height = 0;
        const auto moduleHeadingIsPinned = owner.config.showModuleHeading && owner.config.pinModuleHeading;

        if (owner.config.showCrossoverSolo)
            height += rowHeight;
        if (owner.config.showModuleHeading && ! moduleHeadingIsPinned)
            height += (height > 0 ? verticalGap : 0) + rowHeight;

        for (size_t index = 0; index < tailRowStart;)
        {
            const auto& row = rows[index];
            const auto controlsInRow = juce::jlimit<size_t>(1,
                                                            tailRowStart - index,
                                                            static_cast<size_t>(juce::jmax(1, row->controlsInRow)));
            auto topGap = 0;
            auto preferredHeight = 0;

            for (size_t offset = 0; offset < controlsInRow; ++offset)
            {
                topGap = juce::jmax(topGap, rows[index + offset]->getTopGap());
                preferredHeight = juce::jmax(preferredHeight, rows[index + offset]->getPreferredHeight());
            }

            height += topGap + preferredHeight;
            index += controlsInRow;
        }

        return height + ((! moduleHeadingIsPinned && owner.config.showModuleHeading) || ! rows.empty()
                             ? moduleContentBottomGap
                             : 0);
    }

    juce::Component* getPinnedHeaderComponent() noexcept override
    {
        return owner.config.showModuleHeading && owner.config.pinModuleHeading ? &moduleHeading : nullptr;
    }

    int getPinnedHeaderHeight() const noexcept override
    {
        return owner.config.showModuleHeading && owner.config.pinModuleHeading ? rowHeight : 0;
    }

    juce::Component* getPinnedTailComponent() noexcept override
    {
        return pinnedTail.get();
    }

    int getPinnedTailHeight() const noexcept override
    {
        auto height = 0;

        for (size_t index = tailRowStart; index < rows.size();)
        {
            const auto controlsInRow = juce::jlimit<size_t>(1,
                                                            rows.size() - index,
                                                            static_cast<size_t>(juce::jmax(1, rows[index]->controlsInRow)));
            auto topGap = 0;
            auto preferredHeight = 0;

            for (size_t offset = 0; offset < controlsInRow; ++offset)
            {
                topGap = juce::jmax(topGap, rows[index + offset]->getTopGap());
                preferredHeight = juce::jmax(preferredHeight, rows[index + offset]->getPreferredHeight());
            }

            height += topGap + preferredHeight;
            index += controlsInRow;
        }

        return height;
    }

    void layoutPinnedTail() override
    {
        if (pinnedTail == nullptr)
            return;

        auto bounds = pinnedTail->getLocalBounds();

        for (size_t index = tailRowStart; index < rows.size();)
        {
            auto& row = rows[index];
            const auto controlsInRow = juce::jlimit<size_t>(1,
                                                            rows.size() - index,
                                                            static_cast<size_t>(juce::jmax(1, row->controlsInRow)));
            auto topGap = 0;
            auto preferredHeight = 0;

            for (size_t offset = 0; offset < controlsInRow; ++offset)
            {
                topGap = juce::jmax(topGap, rows[index + offset]->getTopGap());
                preferredHeight = juce::jmax(preferredHeight, rows[index + offset]->getPreferredHeight());
            }

            bounds.removeFromTop(juce::jmin(topGap, bounds.getHeight()));
            auto controlBounds = bounds.removeFromTop(preferredHeight);
            const auto availableWidth = juce::jmax(0,
                                                   controlBounds.getWidth()
                                                       - (parameterGap * static_cast<int>(controlsInRow - 1)));
            const auto controlWidth = availableWidth / static_cast<int>(controlsInRow);

            for (size_t offset = 0; offset < controlsInRow; ++offset)
            {
                const auto isLast = offset + 1 == controlsInRow;
                rows[index + offset]->setBounds(controlBounds.removeFromLeft(isLast ? controlBounds.getWidth()
                                                                                    : controlWidth));

                if (! isLast)
                    controlBounds.removeFromLeft(juce::jmin(parameterGap, controlBounds.getWidth()));
            }

            index += controlsInRow;
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        if (owner.config.showCrossoverSolo)
            soloButton.setBounds(bounds.removeFromTop(rowHeight));

        if (owner.config.showModuleHeading && ! owner.config.pinModuleHeading)
        {
            if (owner.config.showCrossoverSolo && ! bounds.isEmpty())
                bounds.removeFromTop(verticalGap);

            moduleHeading.setBounds(bounds.removeFromTop(rowHeight));
        }

        const auto layoutRows = [this] (const size_t firstRow,
                                        const size_t lastRow,
                                        juce::Rectangle<int> rowBounds)
        {
            for (size_t index = firstRow; index < lastRow;)
            {
                auto& row = rows[index];
                const auto controlsInRow = juce::jlimit<size_t>(1,
                                                                lastRow - index,
                                                                static_cast<size_t>(juce::jmax(1, row->controlsInRow)));
                auto topGap = 0;
                auto preferredHeight = 0;

                for (size_t offset = 0; offset < controlsInRow; ++offset)
                {
                    topGap = juce::jmax(topGap, rows[index + offset]->getTopGap());
                    preferredHeight = juce::jmax(preferredHeight, rows[index + offset]->getPreferredHeight());
                }

                if (! rowBounds.isEmpty())
                    rowBounds.removeFromTop(juce::jmin(topGap, rowBounds.getHeight()));

                auto controlBounds = rowBounds.removeFromTop(preferredHeight);
                const auto availableWidth = juce::jmax(0,
                                                       controlBounds.getWidth()
                                                           - (parameterGap * static_cast<int>(controlsInRow - 1)));
                const auto controlWidth = availableWidth / static_cast<int>(controlsInRow);

                for (size_t offset = 0; offset < controlsInRow; ++offset)
                {
                    const auto isLast = offset + 1 == controlsInRow;
                    rows[index + offset]->setBounds(controlBounds.removeFromLeft(isLast ? controlBounds.getWidth()
                                                                                        : controlWidth));

                    if (! isLast)
                        controlBounds.removeFromLeft(juce::jmin(parameterGap, controlBounds.getWidth()));
                }

                index += controlsInRow;
            }
        };

        layoutRows(0, tailRowStart, bounds);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        owner.clearFocus();
    }

private:
    struct RowBase : public juce::Component
    {
        virtual int getPreferredHeight() const = 0;
        virtual void refreshExternalState() {}
        int getTopGap() const noexcept { return juce::jmax(0, topGapMultiplier) * verticalGap; }

        void mouseDown(const juce::MouseEvent&) override
        {
            shell_parameter_focus::clearFocus(*this);
        }

        int topGapMultiplier = 1;
        int controlsInRow = 1;
        juce::String reorderGroup;
        juce::String orderParameterId;
        bool fixedOrder = false;
    };

    struct ParameterRow final : public RowBase
    {
        ParameterRow(CrossoverRangePage& pageIn,
                     CrossoverModuleComponent& ownerIn,
                     juce::String parameterId,
                     juce::String auxiliaryToggleParameterId,
                     juce::String enabledWhenParameterId,
                     const CrossoverControlSpec& spec)
            : page(pageIn),
              owner(ownerIn),
              auxiliaryToggleId(std::move(auxiliaryToggleParameterId)),
              enabledWhenId(std::move(enabledWhenParameterId))
        {
            control = std::make_unique<ParameterControl>(
                ownerIn.valueTreeState,
                parameterId,
                spec.label,
                spec.decimals);
            topGapMultiplier = spec.topGapMultiplier;
            reorderGroup = spec.reorderGroup != nullptr ? spec.reorderGroup : "";
            fixedOrder = spec.fixedOrder;
            auxiliaryToggleInverted = spec.auxiliaryToggleInverted;
            parameterTitleWidth = spec.parameterTitleWidth;
            auxiliaryToggleWidth = spec.auxiliaryToggleWidth;

            if (spec.orderSuffix != nullptr && juce::String(spec.orderSuffix).isNotEmpty())
                orderParameterId = owner.config.makeRangeParameterId(page.rangeIndex, spec.orderSuffix);

            addAndMakeVisible(*control);

            if (enabledWhenId.isNotEmpty())
                control->setInteractionEnabled(readRawParameter(owner.valueTreeState, enabledWhenId, 0.0f) >= 0.5f);

            if (reorderGroup.isNotEmpty())
            {
                if (owner.config.moduleKey == "tls" && reorderGroup == "gain")
                {
                    orderLabel = makeTextButton("00", uiGrey500);
                    orderLabel->setFillVisible(false);
                    orderLabel->setPressFillEnabled(false);
                    addAndMakeVisible(*orderLabel);
                    refreshOrderLabel();

                    if (! fixedOrder)
                    {
                        orderLabel->onClick = [this] { page.applyReorderMove(*this); };
                        control->setTitleMoveArmedAction([this] { page.armReorderMove(*this); });
                    }
                }
            }

            if (auxiliaryToggleId.isNotEmpty())
            {
                auxiliaryToggle = makeTextButton(spec.auxiliaryToggleLabel);
                auxiliaryToggle->setClickingTogglesState(! auxiliaryToggleInverted);

                if (spec.auxiliaryToggleSymbol != nullptr
                    && juce::String(spec.auxiliaryToggleSymbol).isNotEmpty())
                {
                    auxiliaryToggle->setSystemSymbol(spec.auxiliaryToggleSymbol);
                }

                if (auxiliaryToggleInverted)
                    refreshAuxiliaryToggleState();
                else
                    auxiliaryToggleAttachment = std::make_unique<ButtonAttachment>(owner.valueTreeState,
                                                                                    auxiliaryToggleId,
                                                                                    *auxiliaryToggle);

                auxiliaryToggle->setLongPressPromptActions({}, [this]
                {
                    owner.assignButtonToHostSlot(auxiliaryToggleId, auxiliaryToggleId, auxiliaryToggle.get());
                });

                auxiliaryToggle->onClick = [this]
                {
                    if (auxiliaryToggleInverted)
                        toggleAuxiliaryParameter();

                    owner.clearFocus();
                };
                addAndMakeVisible(*auxiliaryToggle);
            }
        }

        int getPreferredHeight() const override { return rowHeight; }

        void resized() override
        {
            if (control == nullptr)
                return;

            auto bounds = getLocalBounds();

            if (orderLabel != nullptr)
            {
                orderLabel->setBounds(bounds.removeFromLeft(42));
                bounds.removeFromLeft(juce::jmin(parameterGap, bounds.getWidth()));
            }

            if (auxiliaryToggle == nullptr)
            {
                control->setBounds(bounds);
                return;
            }

            const auto toggleWidth = auxiliaryToggle->usesIconOnlyContent()
                ? juce::jmin(iconControlSize, bounds.getWidth())
                : auxiliaryToggleWidth > 0
                    ? juce::jmin(auxiliaryToggleWidth, bounds.getWidth())
                    : juce::jmax(0, (bounds.getWidth() - (parameterGap * 2)) / 3);
            const auto titleWidth = parameterTitleWidth > 0
                ? juce::jmin(parameterTitleWidth,
                             juce::jmax(0, bounds.getWidth() - toggleWidth - (parameterGap * 2)))
                : toggleWidth;
            control->setTitleWidthOverride(titleWidth);
            control->setValueLeadingInset(toggleWidth + parameterGap);
            control->setBounds(bounds);
            auxiliaryToggle->setBounds(bounds.getX() + titleWidth + parameterGap,
                                       bounds.getY(),
                                       toggleWidth,
                                       bounds.getHeight());
        }

        void refreshExternalState() override
        {
            refreshAuxiliaryToggleState();

            if (control != nullptr && enabledWhenId.isNotEmpty())
                control->setInteractionEnabled(readRawParameter(owner.valueTreeState, enabledWhenId, 0.0f) >= 0.5f);

            refreshOrderLabel();

        }

        void refreshOrderLabel()
        {
            if (orderLabel == nullptr)
                return;

            const auto position = fixedOrder ? 1
                                             : juce::roundToInt(readRawParameter(owner.valueTreeState,
                                                                                   orderParameterId,
                                                                                   0.0f)) + 2;
            orderLabel->setButtonText(juce::String::formatted("%02d", position));
        }

        void refreshAuxiliaryToggleState()
        {
            if (auxiliaryToggle == nullptr || ! auxiliaryToggleInverted)
                return;

            const auto parameterEnabled = readRawParameter(owner.valueTreeState, auxiliaryToggleId, 1.0f) >= 0.5f;
            auxiliaryToggle->setToggleState(! parameterEnabled, juce::dontSendNotification);
        }

        void toggleAuxiliaryParameter()
        {
            const auto parameterEnabled = readRawParameter(owner.valueTreeState, auxiliaryToggleId, 1.0f) >= 0.5f;
            owner.setParameterPlainValue(auxiliaryToggleId, parameterEnabled ? 0.0f : 1.0f);
            refreshAuxiliaryToggleState();
        }

        CrossoverRangePage& page;
        CrossoverModuleComponent& owner;
        juce::String auxiliaryToggleId;
        juce::String enabledWhenId;
        bool auxiliaryToggleInverted = false;
        int parameterTitleWidth = 0;
        int auxiliaryToggleWidth = 0;
        std::unique_ptr<ParameterControl> control;
        std::unique_ptr<BoxTextButton> orderLabel;
        std::unique_ptr<BoxTextButton> auxiliaryToggle;
        std::unique_ptr<ButtonAttachment> auxiliaryToggleAttachment;
    };

    struct ChoiceRow final : public RowBase
    {
        ChoiceRow(CrossoverModuleComponent& ownerIn,
                  const juce::String& parameterId,
                  const CrossoverControlSpec& spec)
        {
            control = std::make_unique<ChoiceControl>(ownerIn.valueTreeState, parameterId, spec.label);
            topGapMultiplier = spec.topGapMultiplier;
            addAndMakeVisible(*control);
        }

        int getPreferredHeight() const override { return rowHeight; }

        void resized() override
        {
            if (control != nullptr)
                control->setBounds(getLocalBounds());
        }

        std::unique_ptr<ChoiceControl> control;
    };

    struct HeadingRow final : public RowBase
    {
        explicit HeadingRow(const CrossoverControlSpec& spec)
        {
            heading = makeTextButton(spec.label, uiAccent);
            heading->setClickingTogglesState(false);
            heading->setBorderVisible(true);
            heading->setFillVisible(false);
            heading->setDividerLineVisible(false);
            heading->setPressFillEnabled(false);
            heading->setTextJustification(juce::Justification::centredLeft);
            heading->setInterceptsMouseClicks(false, false);
            topGapMultiplier = spec.topGapMultiplier;
            addAndMakeVisible(*heading);
        }

        int getPreferredHeight() const override { return rowHeight; }

        void resized() override
        {
            if (heading != nullptr)
                heading->setBounds(getLocalBounds());
        }

        std::unique_ptr<BoxTextButton> heading;
    };

    struct InactiveRow final : public RowBase
    {
        explicit InactiveRow(const CrossoverControlSpec& spec)
        {
            button = makeTextButton(spec.label);
            button->setEnabled(false);
            button->setClickingTogglesState(false);
            button->setPressFillEnabled(false);
            button->setInterceptsMouseClicks(false, false);
            topGapMultiplier = spec.topGapMultiplier;
            addAndMakeVisible(*button);
        }

        int getPreferredHeight() const override { return rowHeight; }

        void resized() override
        {
            if (button != nullptr)
                button->setBounds(getLocalBounds());
        }

        std::unique_ptr<BoxTextButton> button;
    };

    struct ToggleRow final : public RowBase
    {
        ToggleRow(CrossoverRangePage& pageIn,
                  CrossoverModuleComponent& ownerIn,
                  juce::String parameterId,
                  const CrossoverControlSpec& spec)
            : page(pageIn),
              owner(ownerIn),
              parameterIdToToggle(std::move(parameterId)),
              exclusiveGroup(spec.exclusiveGroup != nullptr ? spec.exclusiveGroup : ""),
              enabledLabel(spec.enabledLabel != nullptr && juce::String(spec.enabledLabel).isNotEmpty() ? spec.enabledLabel : spec.label),
              disabledLabel(spec.disabledLabel != nullptr && juce::String(spec.disabledLabel).isNotEmpty() ? spec.disabledLabel : spec.label)
        {
            button = makeTextButton(spec.label);
            topGapMultiplier = spec.topGapMultiplier;
            button->setClickingTogglesState(true);
            button->setToggleAccentVisible(spec.toggleAccentVisible);
            attachment = std::make_unique<ButtonAttachment>(owner.valueTreeState, parameterIdToToggle, *button);
            button->setLongPressPromptActions({}, [this]
            {
                owner.assignButtonToHostSlot(parameterIdToToggle, parameterIdToToggle, button.get());
            });
            button->onStateChange = [this] { updateLabel(); };
            button->onClick = [this]
            {
                if (button != nullptr && button->getToggleState() && exclusiveGroup.isNotEmpty())
                    page.clearExclusiveToggleGroup(parameterIdToToggle, exclusiveGroup);

                updateLabel();
                owner.clearFocus();
            };
            addAndMakeVisible(*button);
            updateLabel();
        }

        int getPreferredHeight() const override { return rowHeight; }

        void refreshExternalState() override
        {
            updateLabel();
        }

        void resized() override
        {
            if (button != nullptr)
                button->setBounds(getLocalBounds());
        }

        void updateLabel()
        {
            if (button != nullptr)
                button->setButtonText(button->getToggleState() ? disabledLabel : enabledLabel);
        }

        CrossoverRangePage& page;
        CrossoverModuleComponent& owner;
        juce::String parameterIdToToggle;
        juce::String exclusiveGroup;
        juce::String enabledLabel;
        juce::String disabledLabel;
        std::unique_ptr<BoxTextButton> button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    struct ReadoutRow final : public RowBase
    {
        ReadoutRow(CrossoverModuleComponent& ownerIn,
                   juce::String degreeParameterId,
                   juce::String flipParameterId,
                   const CrossoverControlSpec& spec)
            : owner(ownerIn),
              degreeParameterIdToRead(std::move(degreeParameterId)),
              flipParameterIdToRead(std::move(flipParameterId))
        {
            value.setFont(makeUiFont());
            value.setColour(juce::Label::textColourId, uiWhite);
            value.setColour(juce::Label::backgroundColourId, uiBlack);
            value.setColour(juce::Label::outlineColourId, uiGrey500);
            value.setJustificationType(juce::Justification::centred);
            value.setBorderSize(juce::BorderSize<int> { 1 });
            value.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(value);

            topGapMultiplier = spec.topGapMultiplier;
            updateText();
        }

        int getPreferredHeight() const override { return rowHeight; }

        void refreshExternalState() override
        {
            updateText();
        }

        void resized() override
        {
            value.setBounds(getLocalBounds());
        }

        void updateText()
        {
            const auto degree = readRawParameter(owner.valueTreeState, degreeParameterIdToRead, 0.0f);
            const auto flipRight = readRawParameter(owner.valueTreeState, flipParameterIdToRead, 0.0f) >= 0.5f;
            value.setText(getOrthogonalPositionDescription(degree, flipRight), juce::dontSendNotification);
        }

        CrossoverModuleComponent& owner;
        juce::String degreeParameterIdToRead;
        juce::String flipParameterIdToRead;
        juce::Label value;
    };

    struct TimeRow final : public RowBase
    {
        TimeRow(CrossoverModuleComponent& ownerIn,
                juce::String valueParameterId,
                juce::String modeParameterId,
                juce::String syncParameterId,
                const CrossoverControlSpec& spec)
            : owner(ownerIn),
              valueParameterIdToEdit(std::move(valueParameterId)),
              modeParameterIdToEdit(std::move(modeParameterId)),
              syncParameterIdToEdit(std::move(syncParameterId))
        {
            control = std::make_unique<ParameterControl>(
                owner.valueTreeState,
                valueParameterIdToEdit,
                spec.label,
                spec.decimals);
            topGapMultiplier = spec.topGapMultiplier;
            addAndMakeVisible(*control);

            if (spec.showTimeModeButton)
            {
                modeButton = makeTimeModeButton();
                modeButton->setLongPressPromptActions({}, [this]
                {
                    owner.assignButtonToHostSlot(modeParameterIdToEdit, modeParameterIdToEdit, modeButton.get());
                });
                modeButton->onClick = [this]
                {
                    owner.setParameterPlainValue(modeParameterIdToEdit, isHostSyncMode() ? 0.0f : 1.0f);
                    updateModeControl();
                    owner.clearFocus();
                };
                addAndMakeVisible(*modeButton);
            }
            updateModeControl();
        }

        int getPreferredHeight() const override { return rowHeight; }

        void refreshExternalState() override
        {
            updateModeControl();
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            if (control != nullptr)
            {
                if (modeButton != nullptr)
                {
                    const auto labelZoneWidth = getScaledParameterNameWidth(bounds.getWidth());
                    const auto modeButtonWidth = juce::jmin(rowHeight, labelZoneWidth);
                    const auto titleWidth = juce::jmax(0, labelZoneWidth - modeButtonWidth - parameterGap);
                    control->setTitleWidthOverride(titleWidth);
                    control->setValueLeadingInset(modeButtonWidth + parameterGap);
                    modeButton->setBounds(bounds.getX() + titleWidth + parameterGap,
                                          bounds.getY(),
                                          modeButtonWidth,
                                          bounds.getHeight());
                }
                else
                {
                    control->setTitleWidthOverride(-1);
                    control->setValueLeadingInset(0);
                }

                control->setBounds(bounds);
            }
        }

        bool isHostSyncMode() const noexcept
        {
            return getTimeModeIndex() > 0;
        }

        int getTimeModeIndex() const noexcept
        {
            return juce::jmax(0,
                              static_cast<int>(std::round(readRawParameter(owner.valueTreeState,
                                                                            modeParameterIdToEdit,
                                                                            0.0f))));
        }

        int getSyncChoiceIndex() const noexcept
        {
            const auto choices = owner.config.getHostSyncChoices != nullptr
                ? owner.config.getHostSyncChoices()
                : juce::StringArray {};
            const auto fallback = owner.config.getDefaultHostSyncChoiceIndex != nullptr
                ? static_cast<float>(owner.config.getDefaultHostSyncChoiceIndex())
                : 0.0f;
            const auto rawValue = readRawParameter(owner.valueTreeState, syncParameterIdToEdit, fallback);
            return choices.isEmpty() ? 0
                                     : juce::jlimit(0, choices.size() - 1, static_cast<int>(std::round(rawValue)));
        }

        juce::String getSyncChoiceText() const
        {
            const auto choices = getSyncChoices();

            return choices.isEmpty() ? juce::String {} : choices[getSyncChoiceIndex()];
        }

        juce::StringArray getSyncChoices() const
        {
            auto choices = owner.config.getHostSyncChoices != nullptr
                ? owner.config.getHostSyncChoices()
                : juce::StringArray {};
            const auto typeIndex = getTimeModeIndex();

            if (typeIndex == 2)
                for (auto& choice : choices)
                    choice << "T";
            else if (typeIndex == 3)
                for (auto& choice : choices)
                    choice << ".";

            return choices;
        }

        void showSyncPrompt()
        {
            if (owner.config.showChoicePrompt == nullptr || control == nullptr)
                return;

            const auto choices = getSyncChoices();

            if (choices.isEmpty())
                return;

            std::vector<bool> itemEnabledStates(static_cast<size_t>(choices.size()), true);
            owner.config.showChoicePrompt(owner.getLocalArea(control.get(), control->getValueBounds()),
                                          choices,
                                          getSyncChoiceIndex(),
                                          std::move(itemEnabledStates),
                                          juce::Justification::centred,
                                          [this] (const int choiceIndex)
                                          {
                                              owner.setParameterPlainValue(syncParameterIdToEdit, static_cast<float>(choiceIndex));
                                              updateModeControl();
                                              owner.clearFocus();
                                          },
                                          {},
                                          [this]
                                          {
                                              owner.clearFocus();
                                          });
        }

        void updateModeControl()
        {
            if (control == nullptr)
                return;

            const auto hostSync = isHostSyncMode();
            if (modeButton != nullptr)
            {
                modeButton->setButtonText(hostSync ? "T" : "M");
                modeButton->setAlwaysAccentOutline(hostSync);
            }

            if (hostSync)
            {
                control->setOverrideText(getSyncChoiceText());
                control->setValueClickAction([this] { showSyncPrompt(); });
                control->setInteractionEnabled(true);
            }
            else
            {
                control->clearOverrideText();
                control->setValueClickAction(nullptr);
                control->setInteractionEnabled(true);
            }
        }

        CrossoverModuleComponent& owner;
        juce::String valueParameterIdToEdit;
        juce::String modeParameterIdToEdit;
        juce::String syncParameterIdToEdit;
        std::unique_ptr<ParameterControl> control;
        std::unique_ptr<BoxTextButton> modeButton;
    };

    juce::String getCrossoverRangeParameterId(const CrossoverControlSpec& spec) const
    {
        const auto sourceRange = spec.sourceRangeIndex >= 0
            ? static_cast<size_t>(juce::jlimit(0, static_cast<int>(CrossoverModuleComponent::numRanges - 1), spec.sourceRangeIndex))
            : rangeIndex;
        return owner.config.makeRangeParameterId(sourceRange, spec.suffix);
    }

    void addControlSpecs(const std::vector<CrossoverControlSpec>& specs, juce::Component& parent)
    {
        for (const auto& spec : specs)
        {
            if (spec.kind == ControlKind::heading)
            {
                auto row = std::make_unique<HeadingRow>(spec);
                row->controlsInRow = spec.controlsInRow;
                parent.addAndMakeVisible(*row);
                rows.push_back(std::move(row));
                continue;
            }

            if (spec.kind == ControlKind::toggle)
            {
                auto row = std::make_unique<ToggleRow>(*this, owner, getCrossoverRangeParameterId(spec), spec);
                row->controlsInRow = spec.controlsInRow;
                parent.addAndMakeVisible(*row);
                rows.push_back(std::move(row));
                continue;
            }

            if (spec.kind == ControlKind::choice)
            {
                auto row = std::make_unique<ChoiceRow>(owner, getCrossoverRangeParameterId(spec), spec);
                row->controlsInRow = spec.controlsInRow;
                parent.addAndMakeVisible(*row);
                rows.push_back(std::move(row));
                continue;
            }

            if (spec.kind == ControlKind::inactive)
            {
                auto row = std::make_unique<InactiveRow>(spec);
                row->controlsInRow = spec.controlsInRow;
                parent.addAndMakeVisible(*row);
                rows.push_back(std::move(row));
                continue;
            }

            if (spec.kind == ControlKind::time)
            {
                const auto valueId = getCrossoverRangeParameterId(spec);
                const auto modeId = owner.config.makeRangeParameterId(rangeIndex, spec.modeSuffix);
                const auto syncId = owner.config.makeRangeParameterId(rangeIndex, spec.syncSuffix);
                auto row = std::make_unique<TimeRow>(owner, valueId, modeId, syncId, spec);
                row->controlsInRow = spec.controlsInRow;
                listenedParameterIds.push_back(modeId);
                listenedParameterIds.push_back(syncId);
                owner.valueTreeState.addParameterListener(modeId, this);
                owner.valueTreeState.addParameterListener(syncId, this);
                parent.addAndMakeVisible(*row);
                rows.push_back(std::move(row));
                continue;
            }

            if (spec.kind == ControlKind::readout)
            {
                const auto degreeId = getCrossoverRangeParameterId(spec);
                const auto flipId = owner.config.makeRangeParameterId(rangeIndex, spec.modeSuffix);
                auto row = std::make_unique<ReadoutRow>(owner, degreeId, flipId, spec);
                row->controlsInRow = spec.controlsInRow;
                listenedParameterIds.push_back(degreeId);
                listenedParameterIds.push_back(flipId);
                owner.valueTreeState.addParameterListener(degreeId, this);
                owner.valueTreeState.addParameterListener(flipId, this);
                parent.addAndMakeVisible(*row);
                rows.push_back(std::move(row));
                continue;
            }

            const auto auxiliaryToggleId = spec.auxiliaryToggleSuffix != nullptr
                                               && juce::String(spec.auxiliaryToggleSuffix).isNotEmpty()
                ? owner.config.makeRangeParameterId(rangeIndex, spec.auxiliaryToggleSuffix)
                : juce::String {};
            const auto sourceRange = spec.sourceRangeIndex >= 0
                ? static_cast<size_t>(juce::jlimit(0, static_cast<int>(CrossoverModuleComponent::numRanges - 1), spec.sourceRangeIndex))
                : rangeIndex;
            const auto enabledWhenId = spec.enabledWhenSuffix != nullptr
                                            && juce::String(spec.enabledWhenSuffix).isNotEmpty()
                ? owner.config.makeRangeParameterId(sourceRange, spec.enabledWhenSuffix)
                : juce::String {};
            auto row = std::make_unique<ParameterRow>(*this,
                                                      owner,
                                                      getCrossoverRangeParameterId(spec),
                                                      auxiliaryToggleId,
                                                      enabledWhenId,
                                                      spec);
            row->controlsInRow = spec.controlsInRow;

            if (auxiliaryToggleId.isNotEmpty() && spec.auxiliaryToggleInverted)
            {
                listenedParameterIds.push_back(auxiliaryToggleId);
                owner.valueTreeState.addParameterListener(auxiliaryToggleId, this);
            }

            if (enabledWhenId.isNotEmpty())
            {
                listenedParameterIds.push_back(enabledWhenId);
                owner.valueTreeState.addParameterListener(enabledWhenId, this);
            }

            if (row->orderParameterId.isNotEmpty())
            {
                listenedParameterIds.push_back(row->orderParameterId);
                owner.valueTreeState.addParameterListener(row->orderParameterId, this);
            }

            parent.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
        }

        reorderRows("gain");
    }

    void moveReorderRow(ParameterRow& sourceRow, const int delta)
    {
        if (sourceRow.fixedOrder || sourceRow.orderParameterId.isEmpty() || delta == 0)
            return;

        const auto sourceOrder = juce::roundToInt(
            readRawParameter(owner.valueTreeState, sourceRow.orderParameterId, 0.0f));
        const auto destinationOrder = sourceOrder + delta;

        if (! juce::isPositiveAndBelow(destinationOrder, 4))
            return;

        for (auto& row : rows)
        {
            if (row.get() == &sourceRow
                || row->reorderGroup != sourceRow.reorderGroup
                || row->orderParameterId.isEmpty())
            {
                continue;
            }

            if (juce::roundToInt(readRawParameter(owner.valueTreeState,
                                                  row->orderParameterId,
                                                  -1.0f)) == destinationOrder)
            {
                if (owner.swapParameterPlainValues(sourceRow.orderParameterId,
                                                   row->orderParameterId))
                {
                    reorderRows(sourceRow.reorderGroup);
                    refreshExternalState();
                    owner.refreshCurrentPageLayout();
                }

                return;
            }
        }
    }

    void armReorderMove(ParameterRow& sourceRow)
    {
        if (sourceRow.fixedOrder || sourceRow.orderParameterId.isEmpty())
            return;

        reorderMoveSource = &sourceRow;

        for (const auto& row : rows)
        {
            auto* parameterRow = dynamic_cast<ParameterRow*>(row.get());

            if (parameterRow != nullptr
                && parameterRow->reorderGroup == sourceRow.reorderGroup
                && parameterRow->orderLabel != nullptr)
            {
                parameterRow->orderLabel->setDragTargetOutlineVisible(parameterRow == &sourceRow);
            }
        }
    }

    void applyReorderMove(ParameterRow& destinationRow)
    {
        auto* sourceRow = reorderMoveSource;
        reorderMoveSource = nullptr;

        const auto group = sourceRow != nullptr ? sourceRow->reorderGroup
                                                : destinationRow.reorderGroup;
        clearReorderDragTarget(group);

        if (sourceRow == nullptr
            || sourceRow == &destinationRow
            || sourceRow->fixedOrder
            || destinationRow.fixedOrder
            || sourceRow->orderParameterId.isEmpty()
            || destinationRow.orderParameterId.isEmpty()
            || sourceRow->reorderGroup != destinationRow.reorderGroup)
        {
            return;
        }

        const auto destinationOrder = juce::roundToInt(
            readRawParameter(owner.valueTreeState, destinationRow.orderParameterId, 0.0f));

        for (auto moveCount = 0; moveCount < 4; ++moveCount)
        {
            const auto sourceOrder = juce::roundToInt(
                readRawParameter(owner.valueTreeState, sourceRow->orderParameterId, 0.0f));

            if (sourceOrder == destinationOrder)
            {
                if (sourceRow->orderLabel != nullptr)
                    sourceRow->orderLabel->flashConfirmationOutline();

                owner.clearFocus();
                return;
            }

            moveReorderRow(*sourceRow, destinationOrder > sourceOrder ? 1 : -1);
        }
    }

    void clearReorderDragTarget(const juce::String& group)
    {
        for (const auto& row : rows)
        {
            auto* parameterRow = dynamic_cast<ParameterRow*>(row.get());

            if (parameterRow != nullptr
                && parameterRow->reorderGroup == group
                && parameterRow->orderLabel != nullptr)
            {
                parameterRow->orderLabel->setDragTargetOutlineVisible(false);
            }
        }
    }

    bool reorderRows(const juce::String& group)
    {
        auto first = std::find_if(rows.begin(), rows.end(), [&group] (const auto& row)
        {
            return row->reorderGroup == group;
        });

        if (first == rows.end())
            return false;

        auto last = first;

        while (last != rows.end() && (*last)->reorderGroup == group)
            ++last;

        std::vector<RowBase*> previousOrder;
        previousOrder.reserve(static_cast<size_t>(std::distance(first, last)));

        for (auto current = first; current != last; ++current)
            previousOrder.push_back(current->get());

        std::stable_sort(first, last, [this] (const auto& firstRow, const auto& secondRow)
        {
            const auto getOrder = [this] (const auto& row)
            {
                return row->fixedOrder
                    ? -1
                    : juce::roundToInt(readRawParameter(owner.valueTreeState,
                                                        row->orderParameterId,
                                                        0.0f));
            };

            return getOrder(firstRow) < getOrder(secondRow);
        });

        return ! std::equal(first,
                            last,
                            previousOrder.begin(),
                            [] (const auto& row, const auto* previous) { return row.get() == previous; });
    }

    void clearExclusiveToggleGroup(const juce::String& activeParameterId, const juce::String& exclusiveGroup)
    {
        if (exclusiveGroup.isEmpty())
            return;

        for (auto& row : rows)
        {
            auto* toggleRow = dynamic_cast<ToggleRow*>(row.get());

            if (toggleRow == nullptr
                || toggleRow->exclusiveGroup != exclusiveGroup
                || toggleRow->parameterIdToToggle == activeParameterId)
            {
                continue;
            }

            owner.setParameterPlainValue(toggleRow->parameterIdToToggle, 0.0f);
        }
    }

    void refreshSoloButtonState()
    {
        const auto enabled = ! owner.autoSoloEnabled && owner.getActiveRangeCount() > 1;
        soloButton.setEnabled(enabled);
        soloButton.setAlpha(1.0f);
        soloButton.setToggleState(enabled && owner.isRangeSoloEnabled(rangeIndex), juce::dontSendNotification);
    }

    void updateToggleLabels()
    {
        for (auto& row : rows)
            row->refreshExternalState();
    }

    void updateTimeModeControls()
    {
        for (auto& row : rows)
            row->refreshExternalState();
    }

    void parameterChanged(const juce::String& parameterID, float) override
    {
        if (std::find(listenedParameterIds.begin(), listenedParameterIds.end(), parameterID) == listenedParameterIds.end())
            return;

        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<CrossoverRangePage>(this)]
        {
            if (safeThis != nullptr)
            {
                safeThis->owner.synchroniseManualSoloMaskFromParameters();
                safeThis->refreshExternalState();
            }
        });
    }

    CrossoverModuleComponent& owner;
    size_t rangeIndex = 0;
    BoxTextButton soloButton;
    BoxTextButton moduleHeading;
    std::unique_ptr<juce::Component> pinnedTail;
    std::vector<std::unique_ptr<RowBase>> rows;
    size_t tailRowStart = 0;
    std::vector<juce::String> listenedParameterIds;
    ParameterRow* reorderMoveSource = nullptr;
};

std::unique_ptr<CrossoverModulePage> makeCrossoverRangePage(CrossoverModuleComponent& owner,
                                                            const size_t rangeIndex,
                                                            const juce::Colour accent)
{
    return std::make_unique<CrossoverRangePage>(owner, rangeIndex, accent);
}
