#include "EditorFilterSection.h"
#include "../modules/eql/ProcessorSupport.h"

AvaAudioProcessorEditor::FilterSection::FilterSection(juce::AudioProcessorValueTreeState& state, const int filterIndexIn)
    : header(std::make_unique<BoxTextButton>(uiAccent)),
      typeControl(std::make_unique<ChoiceControl>(state,
                                                  EqlModuleProcessor::getFilterTypeParamId(filterIndexIn),
                                                  "TYPE",
                                                  std::vector<int> { 0, 1, 2, 3, 4, 5, 6 })),
      placeControl(std::make_unique<ChoiceControl>(state,
                                                  EqlModuleProcessor::getFilterPlaceParamId(filterIndexIn),
                                                  "PLACE",
                                                  std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7 })),
      slopeControl(std::make_unique<ChoiceControl>(state,
                                                   EqlModuleProcessor::getFilterSlopeParamId(filterIndexIn),
                                                   "ORDER",
                                                   std::vector<int> { 0, 1, 2, 3, 4, 5 })),
      frequencyControl(std::make_unique<ParameterControl>(state,
                                                          EqlModuleProcessor::getFilterFrequencyParamId(filterIndexIn),
                                                          "FREQ",
                                                          2)),
      bandwidthControl(std::make_unique<ParameterControl>(state,
                                                          EqlModuleProcessor::getFilterBandwidthParamId(filterIndexIn),
                                                          "BW",
                                                          2)),
      gainControl(std::make_unique<ParameterControl>(state,
                                                     EqlModuleProcessor::getFilterGainParamId(filterIndexIn),
                                                     "GAIN",
                                                     2)),
      bypassButton(std::make_unique<BoxTextButton>(uiAccent)),
      filterIndex(filterIndexIn)
{
    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterTypeParamId(filterIndexIn)))
        typeParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterPlaceParamId(filterIndexIn)))
        placeParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterSlopeParamId(filterIndexIn)))
        slopeParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterFrequencyParamId(filterIndexIn)))
        frequencyParameter = dynamic_cast<juce::AudioParameterFloat*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterBandwidthParamId(filterIndexIn)))
        bandwidthParameter = dynamic_cast<juce::AudioParameterFloat*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterGainParamId(filterIndexIn)))
        gainParameter = dynamic_cast<juce::AudioParameterFloat*>(parameter);

    header->setButtonText({});
    header->setTextJustification(juce::Justification::centred);
    header->setClickingTogglesState(false);
    header->setToggleAccentVisible(true);
    header->setCancelClickOnLeave(true);


    bypassButton->setButtonText("B");
    bypassButton->setTextJustification(juce::Justification::centred);
    bypassButton->setClickingTogglesState(true);
    bypassButton->setCancelClickOnLeave(true);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        state,
        EqlModuleProcessor::getFilterBypassParamId(filterIndexIn),
        *bypassButton);
    bypassButton->setLongPressPromptActions({}, [this]
    {
        auto* editor = bypassButton != nullptr
            ? bypassButton->findParentComponentOfClass<AvaAudioProcessorEditor>()
            : nullptr;

        if (editor == nullptr)
            return;

        const auto parameterId = EqlModuleProcessor::getFilterBypassParamId(filterIndex);

        if (auto* parameter = editor->findHostAssignableParameter(parameterId))
            editor->handleHostSlotAssignRequest(parameterId, "B", parameter->getValue());
    });
    lastFilterType = getFilterType();
    slopeControl->setChoices(getBellSlopeDisplayChoicesForType(lastFilterType));
    slopeControl->setChoiceEnabled(0, lastFilterType != FilterType::bell);
    updatePlaceChoicesForType(true);
}

void AvaAudioProcessorEditor::FilterSection::detach() noexcept
{
    if (typeControl != nullptr)
        typeControl->detach();

    if (placeControl != nullptr)
        placeControl->detach();

    if (slopeControl != nullptr)
        slopeControl->detach();

    if (frequencyControl != nullptr)
        frequencyControl->detach();

    if (bandwidthControl != nullptr)
        bandwidthControl->detach();

    if (gainControl != nullptr)
        gainControl->detach();

    bypassAttachment.reset();
    typeParameter = nullptr;
    placeParameter = nullptr;
    slopeParameter = nullptr;
    frequencyParameter = nullptr;
    bandwidthParameter = nullptr;
    gainParameter = nullptr;
}

void AvaAudioProcessorEditor::FilterSection::rebind(juce::AudioProcessorValueTreeState& state)
{
    typeControl->rebind(state);
    placeControl->rebind(state);
    slopeControl->rebind(state);
    frequencyControl->rebind(state);
    bandwidthControl->rebind(state);
    gainControl->rebind(state);

    typeParameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(EqlModuleProcessor::getFilterTypeParamId(filterIndex)));
    placeParameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(EqlModuleProcessor::getFilterPlaceParamId(filterIndex)));
    slopeParameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(EqlModuleProcessor::getFilterSlopeParamId(filterIndex)));
    frequencyParameter = dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(EqlModuleProcessor::getFilterFrequencyParamId(filterIndex)));
    bandwidthParameter = dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(EqlModuleProcessor::getFilterBandwidthParamId(filterIndex)));
    gainParameter = dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(EqlModuleProcessor::getFilterGainParamId(filterIndex)));

    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        state,
        EqlModuleProcessor::getFilterBypassParamId(filterIndex),
        *bypassButton);

    lastFilterType = getFilterType();
    slopeControl->setChoices(getBellSlopeDisplayChoicesForType(lastFilterType));
    slopeControl->setChoiceEnabled(0, lastFilterType != FilterType::bell);
    updatePlaceChoicesForType(true);
}

AvaAudioProcessorEditor::FilterSection::FilterType AvaAudioProcessorEditor::FilterSection::getFilterType() const noexcept
{
    if (typeParameter == nullptr)
        return FilterType::bell;

    return EqlModuleProcessor::filterTypeFromChoiceIndex(typeParameter->getIndex());
}

int AvaAudioProcessorEditor::FilterSection::getPlace() const noexcept
{
    return placeParameter != nullptr ? placeParameter->getIndex()
                                    : 0;
}

double AvaAudioProcessorEditor::FilterSection::getFrequency() const noexcept
{
    return frequencyParameter != nullptr ? static_cast<double>(frequencyParameter->get())
                                         : 0.0;
}

bool AvaAudioProcessorEditor::FilterSection::isBandwidthInactiveAtCurrentSlope() const noexcept
{
    const auto filterType = getFilterType();
    if (filterType == FilterType::volume)
        return true;

    if (filterType == FilterType::bell)
        return slopeParameter != nullptr && slopeParameter->getIndex() == 0;

    const auto slope = slopeParameter != nullptr
        ? EqlModuleProcessor::getBellSlopeValueForChoiceIndex(slopeParameter->getIndex())
        : EqlModuleProcessor::fixedSlopeDbPerOct;

    if (filterType == FilterType::tilt)
        return true;

    return filterType != FilterType::bell
        && (slope <= 6.05f || slope > 96.0f);
}

bool AvaAudioProcessorEditor::FilterSection::isSlopeInactive() const noexcept
{
    const auto filterType = getFilterType();
    return filterType == FilterType::tilt
        || filterType == FilterType::volume;
}

bool AvaAudioProcessorEditor::FilterSection::isGainInactive() const noexcept
{
    const auto filterType = getFilterType();
    return filterType == FilterType::lowCut
        || filterType == FilterType::highCut;
}

void AvaAudioProcessorEditor::FilterSection::updateFrequencyRangeForType()
{
    if (frequencyControl == nullptr)
        return;

    const auto maximumFrequency = getFilterType() == FilterType::lowCut
        ? static_cast<double>(maximumLowCutFrequency)
        : static_cast<double>(maximumVisibleFilterFrequency);
    frequencyControl->setValueRange(minimumVisibleFilterFrequency, maximumFrequency, 0.01);

    if (frequencyControl->getValue() > maximumFrequency)
        frequencyControl->setValue(maximumFrequency, true);
}

void AvaAudioProcessorEditor::FilterSection::setGainDisplaysDegrees(const bool shouldDisplayDegrees)
{
    if (gainControl == nullptr)
        return;

    static constexpr auto degreesPerDb = 7.5;
    gainControl->setValueRange(shouldDisplayDegrees ? -180.0 / degreesPerDb : -48.0,
                               shouldDisplayDegrees ? 180.0 / degreesPerDb : 48.0,
                               0.01);

    if (gainDisplaysDegrees == shouldDisplayDegrees)
        return;

    gainDisplaysDegrees = shouldDisplayDegrees;

    if (gainDisplaysDegrees)
    {
        const auto formatDegrees = [] (const double value)
        {
            auto degrees = juce::jlimit(-180.0, 180.0, value * degreesPerDb);
            if (std::abs(degrees) < 0.005)
                degrees = 0.0;

            return juce::String::formatted("%.2f", degrees);
        };

        gainControl->setTitleText("DEG");
        gainControl->setValueTextTransform(
            formatDegrees,
            formatDegrees,
            [] (const juce::String& text)
            {
                const auto degrees = text.retainCharacters("0123456789+-.").getDoubleValue();
                return juce::jlimit(-180.0, 180.0, degrees) / degreesPerDb;
            });

        if (gainControl->getValue() > 24.0)
            gainControl->setValue(24.0, true);
        else if (gainControl->getValue() < -24.0)
            gainControl->setValue(-24.0, true);
    }
    else
    {
        gainControl->setTitleText("GAIN");
        gainControl->clearValueTextTransform();
    }
}

void AvaAudioProcessorEditor::FilterSection::updatePlaceChoicesForType(const bool normalizeSelection)
{
    if (placeControl == nullptr)
        return;

    const auto phasePlaceAllowed = ! isCutFilterType(getFilterType())
        && ! isVolumeFilterType(getFilterType());
    placeControl->setChoiceEnabled(5, phasePlaceAllowed);
    placeControl->setChoiceEnabled(6, phasePlaceAllowed);
    placeControl->setChoiceEnabled(7, phasePlaceAllowed);

    if (normalizeSelection && ! phasePlaceAllowed && isPhasePlaceChoice(getPlace()))
        placeControl->setSelectedChoiceIndex(0, true);
}

void AvaAudioProcessorEditor::FilterSection::setStoredValues(const FilterType type,
                                                           const double frequency,
                                                           const double bandwidth,
                                                           const double slope,
                                                           const int place,
                                                           const bool isCustom) noexcept
{
    const auto index = static_cast<size_t>(EqlModuleProcessor::choiceIndexFromFilterType(type));
    storedFrequencies[index] = frequency;
    storedBandwidths[index] = bandwidth;
    storedSlopes[index] = slope;
    const auto phasePlaceAllowed = ! isCutFilterType(type) && ! isVolumeFilterType(type);
    storedPlace[index] = ! phasePlaceAllowed && isPhasePlaceChoice(place) ? 0 : place;
    storedValuesCustom[index] = isCustom;
}

int AvaAudioProcessorEditor::FilterSection::getStoredPlace(const FilterType type) const noexcept
{
    return storedPlace[static_cast<size_t>(EqlModuleProcessor::choiceIndexFromFilterType(type))];
}

void AvaAudioProcessorEditor::FilterSection::captureCurrentValuesForType(const FilterType type,
                                                                        const bool markCustom) noexcept
{
    if (suppressStoredValueCapture)
        return;

    if (frequencyParameter == nullptr || bandwidthParameter == nullptr || slopeParameter == nullptr)
        return;

    setStoredValues(type,
                    frequencyParameter->get(),
                    bandwidthParameter->get(),
                    EqlModuleProcessor::getBellSlopeValueForChoiceIndex(slopeParameter->getIndex()),
                    placeParameter != nullptr ? placeParameter->getIndex() : getStoredPlace(type),
                    markCustom);
}

void AvaAudioProcessorEditor::FilterSection::captureCurrentValuesForCurrentType(const bool markCustom) noexcept
{
    captureCurrentValuesForType(getFilterType(), markCustom);
    lastFilterType = getFilterType();
}

void AvaAudioProcessorEditor::FilterSection::copyStoredValuesFrom(const FilterSection& other) noexcept
{
    storedFrequencies = other.storedFrequencies;
    storedBandwidths = other.storedBandwidths;
    storedSlopes = other.storedSlopes;
    storedPlace = other.storedPlace;
    storedValuesCustom = other.storedValuesCustom;
    lastFilterType = other.lastFilterType;
    expanded = other.expanded;
}
