#include "Editor.h"
#include "FilterSection.h"
#include "../modules/eql/FilterSupport.h"
#include "../modules/eql/Processor.h"

AvaAudioProcessorEditor::FilterSection::FilterSection(juce::AudioProcessorValueTreeState& state, const int filterIndexIn)
    : header(std::make_unique<BoxTextButton>(uiAccent)),
      typeControl(std::make_unique<ChoiceControl>(state,
                                                  EqlModuleProcessor::getFilterTypeParamId(filterIndexIn),
                                                  "TYPE",
                                                  std::vector<int> { 0, 1, 2, 3, 4, 5, 6 })),
      phaseButton(std::make_unique<BoxTextButton>(uiAccent)),
      orderControl(std::make_unique<ChoiceControl>(state,
                                                   EqlModuleProcessor::getFilterOrderParamId(filterIndexIn),
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

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterPhaseParamId(filterIndexIn)))
        phaseParameter = dynamic_cast<juce::AudioParameterBool*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterOrderParamId(filterIndexIn)))
        orderParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterFrequencyParamId(filterIndexIn)))
        frequencyParameter = dynamic_cast<juce::AudioParameterFloat*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterBandwidthParamId(filterIndexIn)))
        bandwidthParameter = dynamic_cast<juce::AudioParameterFloat*>(parameter);

    if (auto* parameter = state.getParameter(EqlModuleProcessor::getFilterGainParamId(filterIndexIn)))
        gainParameter = dynamic_cast<juce::AudioParameterFloat*>(parameter);

    phaseButton->setTablerIcon("phase");
    phaseButton->setClickingTogglesState(true);
    phaseButton->getProperties().set(juce::Identifier("oscParameterId"), EqlModuleProcessor::getFilterPhaseParamId(filterIndex));
    typeControl->setTitleAccessory(phaseButton.get());
    header->setButtonText({});
    {
        juce::StringArray fallbackParameterIds;
        fallbackParameterIds.add(EqlModuleProcessor::getFilterTypeParamId(filterIndexIn));
        fallbackParameterIds.add(EqlModuleProcessor::getFilterPhaseParamId(filterIndexIn));
        fallbackParameterIds.add(EqlModuleProcessor::getFilterOrderParamId(filterIndexIn));
        fallbackParameterIds.add(EqlModuleProcessor::getFilterFrequencyParamId(filterIndexIn));
        fallbackParameterIds.add(EqlModuleProcessor::getFilterBandwidthParamId(filterIndexIn));
        fallbackParameterIds.add(EqlModuleProcessor::getFilterGainParamId(filterIndexIn));
        header->getProperties().set(juce::Identifier("oscFallbackParameterIds"),
                                    fallbackParameterIds.joinIntoString("\n"));
    }
    header->setTextJustification(juce::Justification::centred);
    header->setClickingTogglesState(false);
    header->setToggleActiveFillEnabled(false);
    header->setCancelClickOnLeave(true);

    bypassButton->getProperties().set(juce::Identifier("oscParameterId"),
                                      EqlModuleProcessor::getFilterBypassParamId(filterIndexIn));
    bypassButton->setButtonText({});
    bypassButton->setTablerIcon("plug-off");
    bypassButton->setTextJustification(juce::Justification::centred);
    bypassButton->setClickingTogglesState(true);
    bypassButton->setCancelClickOnLeave(true);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        state,
        EqlModuleProcessor::getFilterBypassParamId(filterIndexIn),
        *bypassButton);
    phaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        state, EqlModuleProcessor::getFilterPhaseParamId(filterIndex), *phaseButton);
    refreshTypeDependentControls();
}

void AvaAudioProcessorEditor::FilterSection::detach() noexcept
{
    if (typeControl != nullptr)
        typeControl->detach();

    phaseAttachment.reset();

    if (orderControl != nullptr)
        orderControl->detach();

    if (frequencyControl != nullptr)
        frequencyControl->detach();

    if (bandwidthControl != nullptr)
        bandwidthControl->detach();

    if (gainControl != nullptr)
        gainControl->detach();

    bypassAttachment.reset();
    typeParameter = nullptr;
    phaseParameter = nullptr;
    orderParameter = nullptr;
    frequencyParameter = nullptr;
    bandwidthParameter = nullptr;
    gainParameter = nullptr;
}

void AvaAudioProcessorEditor::FilterSection::rebind(juce::AudioProcessorValueTreeState& state)
{
    typeControl->rebind(state);

    orderControl->rebind(state);
    frequencyControl->rebind(state);
    bandwidthControl->rebind(state);
    gainControl->rebind(state);

    typeParameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(EqlModuleProcessor::getFilterTypeParamId(filterIndex)));
    phaseParameter = dynamic_cast<juce::AudioParameterBool*>(state.getParameter(EqlModuleProcessor::getFilterPhaseParamId(filterIndex)));
    orderParameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(EqlModuleProcessor::getFilterOrderParamId(filterIndex)));
    frequencyParameter = dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(EqlModuleProcessor::getFilterFrequencyParamId(filterIndex)));
    bandwidthParameter = dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(EqlModuleProcessor::getFilterBandwidthParamId(filterIndex)));
    gainParameter = dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(EqlModuleProcessor::getFilterGainParamId(filterIndex)));

    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        state,
        EqlModuleProcessor::getFilterBypassParamId(filterIndex),
        *bypassButton);

    phaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        state, EqlModuleProcessor::getFilterPhaseParamId(filterIndex), *phaseButton);
    refreshTypeDependentControls();
}

AvaAudioProcessorEditor::FilterSection::FilterType AvaAudioProcessorEditor::FilterSection::getFilterType() const noexcept
{
    if (typeParameter == nullptr)
        return FilterType::bell;

    return EqlModuleProcessor::filterTypeFromChoiceIndex(typeParameter->getIndex());
}

bool AvaAudioProcessorEditor::FilterSection::isPhaseEnabled() const noexcept
{
    return phaseParameter != nullptr && phaseParameter->get();
}

double AvaAudioProcessorEditor::FilterSection::getFrequency() const noexcept
{
    return frequencyParameter != nullptr ? static_cast<double>(frequencyParameter->get())
                                         : 0.0;
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

    static constexpr auto degreesPerDb = filterPhaseDegreesPerDb;
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

void AvaAudioProcessorEditor::FilterSection::updatePhaseForType(const bool normalizeSelection)
{
    const auto allowed = getFilterControlAvailability(getFilterType(),
        orderParameter != nullptr ? orderParameter->getIndex() : 2).phase;
    phaseButton->setEnabled(allowed);
    if (normalizeSelection && !allowed && isPhaseEnabled() && phaseParameter != nullptr)
        phaseParameter->setValueNotifyingHost(0.0f);
}

void AvaAudioProcessorEditor::FilterSection::refreshTypeDependentControls(const bool normalizePhaseSelection)
{
    const auto filterType = getFilterType();
    orderControl->setChoices(getOrderDisplayChoicesForType(filterType));
    orderControl->setChoiceEnabled(0, filterType != FilterType::bell);
    updatePhaseForType(normalizePhaseSelection);
}
