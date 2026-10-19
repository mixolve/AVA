#include "ModuleBindings.h"
#include "RangePage.h"
#include "UiSupport.h"
#include "UiStateSchema.h"

#include "../shell/LocalParameterControl.h"
#include "../shell/PetalGeometry.h"
#include "../shell/EditorSetup.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace crossover_ui;

int CrossoverRangePage::RowBase::getTopGap() const noexcept
{
    return juce::jmax(0, topGapMultiplier) * verticalGap;
}

void CrossoverRangePage::RowBase::paint(juce::Graphics& graphics)
{
    if (paintsRowBackground) graphics.fillAll(uiBackground);
}

void CrossoverRangePage::RowBase::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        if (auto* page = findParentComponentOfClass<CrossoverRangePage>())
            page->mouseDown(event.getEventRelativeTo(page));
}

void CrossoverRangePage::RowBase::paintOverChildren(juce::Graphics& graphics)
{
    if (exclusiveScope == ExclusiveScope::none) return;
    const auto markerColour = exclusiveScope == ExclusiveScope::domain ? uiGreyLight : uiWhite;
    const auto drawPetal = [&graphics, markerColour] (juce::Component& box, const juce::Rectangle<int> bounds)
    {
        const auto* button = dynamic_cast<BoxTextButton*>(&box);
        if (button == nullptr || ! button->isVisible() || button->usesIconOnlyContent())
            return;
        const auto petal = makeSidePetal(bounds);
        graphics.setColour(markerColour.interpolatedWith(uiGreyDark,button->getActiveFillAmount()));
        graphics.fillPath(petal);
        graphics.setColour(uiGreyLight);
        graphics.strokePath(petal, juce::PathStrokeType(1.0f));
        button->paintOutline(graphics,bounds);
    };
    for (auto* child : getChildren())
    {
        if (dynamic_cast<ParameterControl*>(child) != nullptr || dynamic_cast<ChoiceControl*>(child) != nullptr)
        {
            for (auto* box : child->getChildren())
                drawPetal(*box, getLocalArea(child, box->getBounds()));
        }
        else
            drawPetal(*child, child->getBounds());
    }
}

CrossoverRangePage::ParameterRow::ParameterRow(CrossoverRangePage& pageIn,
                                                CrossoverModuleComponent& ownerIn,
                                                juce::String parameterId,
                                                juce::String auxiliaryToggleParameterId,
                                                const CrossoverControlSpec& spec)
    : page(pageIn),
      owner(ownerIn),
      auxiliaryToggleId(std::move(auxiliaryToggleParameterId)),
      controlSpec(spec)
{
    control = std::make_unique<ParameterControl>(
        ownerIn.valueTreeState,
        parameterId,
        spec.label,
        spec.decimals);
    if(owner.config.moduleKey=="tls" && juce::String(spec.suffix).endsWith(".gain"))
        control->setTitlePopupAction([safe=juce::Component::SafePointer<CrossoverRangePage>(&page)] {
            if(safe!=nullptr) safe->showGainArrange();
        });
    if (owner.config.moduleKey == "dyn"
        && (juce::String(spec.suffix) == "morph" || juce::String(spec.suffix) == "mix" || juce::String(spec.suffix) == "peak-rms" || juce::String(spec.suffix) == "release-type" || juce::String(spec.suffix) == "feedback" || juce::String(spec.suffix) == "clip-knee" || juce::String(spec.suffix) == "comp-knee" || juce::String(spec.suffix) == "range-curve"))
        control->setValueTextTransform({},
            [] (double value) { return juce::String(value, 2); },
            [] (const juce::String& text)
            {
                if (text.trim().equalsIgnoreCase("COMPRESSOR") || text.trim().equalsIgnoreCase("WET") || text.trim().equalsIgnoreCase("RMS") || text.trim().equalsIgnoreCase("TO-SIGNAL") || text.trim().equalsIgnoreCase("FB") || text.trim().equalsIgnoreCase("SOFT"))
                    return 100.0;
                return text.trim().getDoubleValue();
            });
    if (juce::String(spec.auxiliaryChoiceSuffix).isNotEmpty())
    {
        const auto choiceId = owner.config.makeRangeParameterId(page.rangeIndex, spec.auxiliaryChoiceSuffix);
        auxiliaryChoice = std::make_unique<ChoiceControl>(owner.valueTreeState, choiceId, "");
        auxiliaryChoice->setTitleWidthOverride(0);
        addAndMakeVisible(*auxiliaryChoice);
    }
    const auto dependencyRange = spec.sourceRangeIndex >= 0
        ? static_cast<size_t>(spec.sourceRangeIndex) : page.rangeIndex;
    for (const auto* suffix : spec.enabledWhenAnyNonZeroSuffixes)
        enabledWhenAnyNonZeroIds.push_back(owner.config.makeRangeParameterId(dependencyRange, suffix));
    topGapMultiplier = spec.topGapMultiplier;
    auxiliaryToggleInverted = spec.auxiliaryToggleInverted;
    parameterTitleWidth = spec.parameterTitleWidth;
    auxiliaryToggleWidth = spec.auxiliaryToggleWidth;

    control->getProperties().set("domainDisabled", spec.interactionDisabled);
    if (owner.config.moduleKey == "dyn" && juce::String(spec.suffix) == "morph"
        && readRawParameter(owner.valueTreeState, parameterId.upToLastOccurrenceOf("_", true, false) + "domain", 0.0f) >= 0.5f)
        control->setOverrideText("COMPRESSOR");
    addAndMakeVisible(*control);
    if (auxiliaryChoice != nullptr) auxiliaryChoice->toFront(false);

    refreshInteractionState();

    if (auxiliaryToggleId.isNotEmpty())
    {
        auxiliaryToggle = makeTextButton(spec.auxiliaryToggleLabel);
        auxiliaryToggle->getProperties().set(juce::Identifier("oscParameterId"), auxiliaryToggleId);
        auxiliaryToggle->setClickingTogglesState(! auxiliaryToggleInverted);

        if (spec.auxiliaryToggleIcon != nullptr
            && juce::String(spec.auxiliaryToggleIcon).isNotEmpty())
        {
            auxiliaryToggle->setTablerIcon(spec.auxiliaryToggleIcon);
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
        auxiliaryToggle->setEnabled(! spec.interactionDisabled);
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
    const auto valueWidth = getScaledParameterNameWidth(bounds.getWidth());

    if (auxiliaryChoice != nullptr)
    {
        const auto titleZone = getScaledParameterNameWidth(bounds.getWidth());
        const auto choiceWidth = juce::jmin(60, titleZone);
        const auto titleWidth = juce::jmax(0, titleZone - choiceWidth - parameterGap);
        control->setTitleWidthOverride(titleWidth);
        control->setValueLeadingInset(choiceWidth + parameterGap);
        control->setBounds(bounds);
        auxiliaryChoice->setBounds(bounds.getX() + titleWidth + parameterGap, bounds.getY(),
                                   choiceWidth, bounds.getHeight());
        return;
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
    const auto maximumTitleWidth = juce::jmax(0, bounds.getWidth() - toggleWidth - (parameterGap * 2));
    const auto titleWidth = auxiliaryToggle->usesIconOnlyContent()
        ? juce::jlimit(0,
                       maximumTitleWidth,
                       bounds.getWidth()
                           - parameterGap
                           - (toggleWidth + parameterGap)
                           - valueWidth)
        : parameterTitleWidth > 0
            ? juce::jmin(parameterTitleWidth, maximumTitleWidth)
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

    refreshInteractionState();

}

void CrossoverRangePage::ParameterRow::refreshInteractionState()
{
    if (control != nullptr)
        control->setInteractionEnabled(crossover_configs::isRangeControlEnabled(
            controlSpec, owner.valueTreeState, page.rangeIndex, owner.config.moduleKey),
            ! controlSpec.interactionDisabled);
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
                                         const juce::String& auxiliaryToggleId,
                                         const CrossoverControlSpec& spec)
    : owner(ownerIn), suffix(spec.suffix)
{
    control = std::make_unique<ChoiceControl>(ownerIn.valueTreeState, parameterId, spec.label);
    if (ownerIn.config.moduleKey == "dyn")
    {
        const auto prefix = parameterId.upToLastOccurrenceOf("_", true, false);
        if (juce::String(spec.suffix) == "mode")
            control->setChoices(readRawParameter(ownerIn.valueTreeState, prefix + "domain", 0.0f) >= 0.5f
                ? juce::StringArray { "FREQ", "CORR" } : juce::StringArray { "COMP/CLIP", "TRANS/SUST" });
    }
    control->setInteractionEnabled(! spec.interactionDisabled);
    topGapMultiplier = spec.topGapMultiplier;
    addAndMakeVisible(*control);
    if (auxiliaryToggleId.isNotEmpty())
    {
        auxiliaryToggle = makeTextButton(spec.auxiliaryToggleLabel);
        auxiliaryToggle->setClickingTogglesState(true);
        auxiliaryToggle->setTablerIcon(spec.auxiliaryToggleIcon);
        auxiliaryToggle->setTooltip("Listen to side chain");
        auxiliaryToggle->getProperties().set(juce::Identifier("oscParameterId"), auxiliaryToggleId);
        addAndMakeVisible(*auxiliaryToggle);
        auxiliaryToggleAttachment = std::make_unique<ButtonAttachment>(ownerIn.valueTreeState,
            auxiliaryToggleId, *auxiliaryToggle);
        auxiliaryToggle->setLongPressPromptActions({}, [ownerPtr = &ownerIn, auxiliaryToggleId, this]
        {
            ownerPtr->assignButtonToHostSlot(auxiliaryToggleId, auxiliaryToggleId, auxiliaryToggle.get());
        });
    }
    refreshExternalState();
}

void CrossoverRangePage::ChoiceRow::refreshExternalState()
{
    if (!owner.config.isRangeChoiceAvailable) return;
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(
            owner.valueTreeState.getParameter(control->getParameterId())))
        for (int choice = 0; choice < parameter->choices.size(); ++choice)
            control->setChoiceEnabled(choice, owner.config.isRangeChoiceAvailable(suffix, choice));
}

int CrossoverRangePage::ChoiceRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::ChoiceRow::resized()
{
    if (control == nullptr)
        return;
    const auto bounds = getLocalBounds();
    if (auxiliaryToggle != nullptr)
    {
        const auto titleZone = getScaledParameterNameWidth(bounds.getWidth());
        const auto buttonWidth = juce::jmin(auxiliaryToggle->usesIconOnlyContent() ? iconControlSize : 60, titleZone);
        const auto titleWidth = juce::jmax(0, titleZone - buttonWidth - parameterGap);
        control->setTitleWidthOverride(titleWidth);
        control->setValueLeadingInset(buttonWidth + parameterGap);
        auxiliaryToggle->setBounds(bounds.getX() + titleWidth + parameterGap, bounds.getY(),
                                   buttonWidth, bounds.getHeight());
    }
    control->setBounds(bounds);
}

CrossoverRangePage::HeadingRow::HeadingRow(const CrossoverControlSpec& spec)
{
    heading = makeTextButton(spec.label, uiAccent);
    heading->setClickingTogglesState(false);
    heading->setBorderVisible(true);
    heading->setFillVisible(false);
    heading->setDividerLineVisible(false);
    heading->setInteractionOutlineEnabled(false);
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
    button->setInteractionOutlineEnabled(false);
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
    button->getProperties().set(juce::Identifier("oscParameterId"), parameterIdToToggle);
    topGapMultiplier = spec.topGapMultiplier;
    button->setClickingTogglesState(true);
    button->setToggleActiveFillEnabled(spec.toggleActiveFillEnabled);
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
    if (spec.auxiliaryToggleSuffix != nullptr && juce::String(spec.auxiliaryToggleSuffix).isNotEmpty())
    {
        const auto auxiliaryId = owner.config.makeRangeParameterId(page.rangeIndex, spec.auxiliaryToggleSuffix);
        auxiliaryToggleWidth = spec.auxiliaryToggleWidth > 0 ? spec.auxiliaryToggleWidth : 60;
        auxiliaryToggle = makeTextButton(spec.auxiliaryToggleLabel);
        auxiliaryToggle->setClickingTogglesState(true);
        auxiliaryToggle->setTablerIcon(spec.auxiliaryToggleIcon);
        auxiliaryToggle->setTooltip(spec.auxiliaryToggleLabel);
        auxiliaryToggle->getProperties().set(juce::Identifier("oscParameterId"), auxiliaryId);
        auxiliaryToggleAttachment = std::make_unique<ButtonAttachment>(owner.valueTreeState, auxiliaryId, *auxiliaryToggle);
        auxiliaryToggle->setLongPressPromptActions({}, [this, auxiliaryId]
        {
            owner.assignButtonToHostSlot(auxiliaryId, auxiliaryId, auxiliaryToggle.get());
        });
        auxiliaryToggle->onClick = [this] { owner.clearFocus(); };
        addAndMakeVisible(*auxiliaryToggle);
    }
    button->getProperties().set("domainDisabled", spec.interactionDisabled);
    refreshExternalState();
}

int CrossoverRangePage::ToggleRow::getPreferredHeight() const
{
    return rowHeight;
}

void CrossoverRangePage::ToggleRow::refreshExternalState()
{
    const auto disabled = button != nullptr && static_cast<bool>(button->getProperties().getWithDefault("domainDisabled", false));
    if (button != nullptr) button->setEnabled(! disabled);
    if (auxiliaryToggle != nullptr) auxiliaryToggle->setEnabled(! disabled);
    updateLabel();
}

void CrossoverRangePage::ToggleRow::resized()
{
    auto bounds = getLocalBounds();
    if (auxiliaryToggle != nullptr)
    {
        auxiliaryToggle->setBounds(bounds.removeFromRight(juce::jmin(auxiliaryToggleWidth, bounds.getWidth())));
        bounds.removeFromRight(parameterGap);
    }
    if (button != nullptr)
        button->setBounds(bounds);
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
    value.getProperties().set(juce::Identifier("oscParameterId"), degreeParameterIdToRead);
    value.getProperties().set(juce::Identifier("oscParameterId2"), flipParameterIdToRead);
    value.setFont(makeUiFont());
    value.setColour(juce::Label::textColourId, uiWhite);
    value.setColour(juce::Label::backgroundColourId, uiBackground);
    value.setColour(juce::Label::outlineColourId, uiGreyLight);
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
    : controlSpec(spec), owner(ownerIn),
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
        if (owner.config.moduleKey == "dyn") modeButton->setTablerIcon("clef");
        modeButton->getProperties().set(juce::Identifier("oscParameterId"), modeParameterIdToEdit);
        modeButton->getProperties().set(juce::Identifier("oscParameterId2"), syncParameterIdToEdit);
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
        modeButton->setActiveFill(hostSync);
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
    if (owner.config.moduleKey == "dyn")
    {
        const auto prefix = valueParameterIdToEdit.upToLastOccurrenceOf("_", false, false);
        const auto range = static_cast<size_t>(juce::jmax(1, prefix.fromFirstOccurrenceOf("band-", false, false).getIntValue()) - 1);
        const auto enabled = crossover_configs::isRangeControlEnabled(controlSpec, owner.valueTreeState, range, owner.config.moduleKey);
        control->setInteractionEnabled(enabled, ! controlSpec.interactionDisabled);
        if (modeButton != nullptr) modeButton->setEnabled(enabled);
    }
}
