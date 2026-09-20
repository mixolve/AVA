#include "RangePage.h"
#include "UiSupport.h"

#include "../shell/LocalParameterControl.h"

#include <cmath>
#include <utility>

using namespace crossover_ui;

int CrossoverRangePage::RowBase::getTopGap() const noexcept
{
    return juce::jmax(0, topGapMultiplier) * verticalGap;
}

void CrossoverRangePage::RowBase::mouseDown(const juce::MouseEvent&)
{
    shell_parameter_focus::clearFocus(*this);
}

CrossoverRangePage::ParameterRow::ParameterRow(CrossoverRangePage& pageIn,
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

int CrossoverRangePage::ParameterRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::ParameterRow::resized()
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

void CrossoverRangePage::ParameterRow::refreshExternalState()
{
    refreshAuxiliaryToggleState();

    if (control != nullptr && enabledWhenId.isNotEmpty())
        control->setInteractionEnabled(readRawParameter(owner.valueTreeState, enabledWhenId, 0.0f) >= 0.5f);

    refreshOrderLabel();
}

void CrossoverRangePage::ParameterRow::refreshOrderLabel()
{
    if (orderLabel == nullptr)
        return;

    const auto position = fixedOrder ? 1
                                     : juce::roundToInt(readRawParameter(owner.valueTreeState,
                                                                         orderParameterId,
                                                                         0.0f)) + 2;
    orderLabel->setButtonText(juce::String::formatted("%02d", position));
}

void CrossoverRangePage::ParameterRow::refreshAuxiliaryToggleState()
{
    if (auxiliaryToggle == nullptr || ! auxiliaryToggleInverted)
        return;

    const auto parameterEnabled = readRawParameter(owner.valueTreeState, auxiliaryToggleId, 1.0f) >= 0.5f;
    auxiliaryToggle->setToggleState(! parameterEnabled, juce::dontSendNotification);
}

void CrossoverRangePage::ParameterRow::toggleAuxiliaryParameter()
{
    const auto parameterEnabled = readRawParameter(owner.valueTreeState, auxiliaryToggleId, 1.0f) >= 0.5f;
    owner.setParameterPlainValue(auxiliaryToggleId, parameterEnabled ? 0.0f : 1.0f);
    refreshAuxiliaryToggleState();
}

CrossoverRangePage::ChoiceRow::ChoiceRow(CrossoverModuleComponent& ownerIn,
                                         const juce::String& parameterId,
                                         const CrossoverControlSpec& spec)
{
    control = std::make_unique<ChoiceControl>(ownerIn.valueTreeState, parameterId, spec.label);
    topGapMultiplier = spec.topGapMultiplier;
    addAndMakeVisible(*control);
}

int CrossoverRangePage::ChoiceRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::ChoiceRow::resized()
{
    if (control != nullptr)
        control->setBounds(getLocalBounds());
}

CrossoverRangePage::HeadingRow::HeadingRow(const CrossoverControlSpec& spec)
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

int CrossoverRangePage::HeadingRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::HeadingRow::resized()
{
    if (heading != nullptr)
        heading->setBounds(getLocalBounds());
}

CrossoverRangePage::InactiveRow::InactiveRow(const CrossoverControlSpec& spec)
{
    button = makeTextButton(spec.label);
    button->setEnabled(false);
    button->setClickingTogglesState(false);
    button->setPressFillEnabled(false);
    button->setInterceptsMouseClicks(false, false);
    topGapMultiplier = spec.topGapMultiplier;
    addAndMakeVisible(*button);
}

int CrossoverRangePage::InactiveRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::InactiveRow::resized()
{
    if (button != nullptr)
        button->setBounds(getLocalBounds());
}

CrossoverRangePage::ToggleRow::ToggleRow(CrossoverRangePage& pageIn,
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

int CrossoverRangePage::ToggleRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::ToggleRow::refreshExternalState()
{
    updateLabel();
}

void CrossoverRangePage::ToggleRow::resized()
{
    if (button != nullptr)
        button->setBounds(getLocalBounds());
}

void CrossoverRangePage::ToggleRow::updateLabel()
{
    if (button != nullptr)
        button->setButtonText(button->getToggleState() ? disabledLabel : enabledLabel);
}

CrossoverRangePage::ReadoutRow::ReadoutRow(CrossoverModuleComponent& ownerIn,
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

int CrossoverRangePage::ReadoutRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::ReadoutRow::refreshExternalState()
{
    updateText();
}

void CrossoverRangePage::ReadoutRow::resized()
{
    value.setBounds(getLocalBounds());
}

void CrossoverRangePage::ReadoutRow::updateText()
{
    const auto degree = readRawParameter(owner.valueTreeState, degreeParameterIdToRead, 0.0f);
    const auto flipRight = readRawParameter(owner.valueTreeState, flipParameterIdToRead, 0.0f) >= 0.5f;
    value.setText(getOrthogonalPositionDescription(degree, flipRight), juce::dontSendNotification);
}

CrossoverRangePage::TimeRow::TimeRow(CrossoverModuleComponent& ownerIn,
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

int CrossoverRangePage::TimeRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::TimeRow::refreshExternalState()
{
    updateModeControl();
}

void CrossoverRangePage::TimeRow::resized()
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

bool CrossoverRangePage::TimeRow::isHostSyncMode() const noexcept
{
    return getTimeModeIndex() > 0;
}

int CrossoverRangePage::TimeRow::getTimeModeIndex() const noexcept
{
    return juce::jmax(0,
                      static_cast<int>(std::round(readRawParameter(owner.valueTreeState,
                                                                    modeParameterIdToEdit,
                                                                    0.0f))));
}

int CrossoverRangePage::TimeRow::getSyncChoiceIndex() const noexcept
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

juce::String CrossoverRangePage::TimeRow::getSyncChoiceText() const
{
    const auto choices = getSyncChoices();
    return choices.isEmpty() ? juce::String {} : choices[getSyncChoiceIndex()];
}

juce::StringArray CrossoverRangePage::TimeRow::getSyncChoices() const
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

void CrossoverRangePage::TimeRow::showSyncPrompt()
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

void CrossoverRangePage::TimeRow::updateModeControl()
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

