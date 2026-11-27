#include "Editor.h"
#include "FilterSection.h"
#include "../modules/eql/FilterSupport.h"
#include "../modules/eql/Processor.h"

AvaAudioProcessorEditor::FilterSection::FilterSection(juce::AudioProcessorValueTreeState& state, const int filterIndexIn)
    : header(std::make_unique<PushControl>("", false)),
      typeControl(std::make_unique<ChoiceControl>(state,
                                                  EqlModuleProcessor::getFilterTypeParamId(filterIndexIn),
                                                  "FILTER\nTYPE")),
      orderControl(std::make_unique<ChoiceControl>(state,
                                                   EqlModuleProcessor::getFilterOrderParamId(filterIndexIn),
                                                   "FILTER\nORDER")),
      frequencyControl(std::make_unique<ParameterControl>(state,
                                                          EqlModuleProcessor::getFilterFrequencyParamId(filterIndexIn),
                                                          "FILTER\nFREQ",
                                                          0)),
      bandwidthControl(std::make_unique<ParameterControl>(state,
                                                          EqlModuleProcessor::getFilterBandwidthParamId(filterIndexIn),
                                                          "FILTER\nBW",
                                                          2)),
      gainControl(std::make_unique<ParameterControl>(state,
                                                     EqlModuleProcessor::getFilterGainParamId(filterIndexIn),
                                                     "FILTER\nGAIN",
                                                     2)),
      filterIndex(filterIndexIn)
{
    // An EQL filter belongs to its selected band and can change identity when
    // filters are added, removed or reordered. Never allow any of its UI
    // controls (including its header and phase accessory) into the dock.
    header->setBottomIndicatorsVisible(false, false);
    body.getProperties().set("disableDockSelection", true);
    for (auto* control : std::array<juce::Component*, 6> {
            header.get(), typeControl.get(), orderControl.get(),
            frequencyControl.get(), bandwidthControl.get(), gainControl.get() })
        control->getProperties().set("disableDockSelection", true);
    for (auto* control : std::array<ParameterField*, 4> { typeControl.get(), orderControl.get(), frequencyControl.get(), bandwidthControl.get() })
        control->setBottomIndicatorsVisible(true, false);
    phaseIndicator = std::make_unique<AccessoryButton>("PH");
    phaseIndicator->getProperties().set("disableDockSelection", true);
    phaseIndicator->bindParameter(state, EqlModuleProcessor::getFilterPhaseParamId(filterIndex));
    typeControl->setBottomAccessory(phaseIndicator.get());
    cacheParameterPointers(state);
    refreshTypeDependentControls();
}

void AvaAudioProcessorEditor::FilterSection::detach() noexcept
{
    if (typeControl != nullptr)
        typeControl->detach();

    phaseIndicator->detach();

    if (orderControl != nullptr)
        orderControl->detach();

    if (frequencyControl != nullptr)
        frequencyControl->detach();

    if (bandwidthControl != nullptr)
        bandwidthControl->detach();

    if (gainControl != nullptr)
        gainControl->detach();

    typeParameter = nullptr;
    phaseParameter = nullptr;
    orderParameter = nullptr;
    frequencyParameter = nullptr;
}

void AvaAudioProcessorEditor::FilterSection::rebind(juce::AudioProcessorValueTreeState& state)
{
    typeControl->rebind(state);

    orderControl->rebind(state);
    frequencyControl->rebind(state);
    bandwidthControl->rebind(state);
    gainControl->rebind(state);

    cacheParameterPointers(state);
    phaseIndicator->bindParameter(state, EqlModuleProcessor::getFilterPhaseParamId(filterIndex));
    refreshTypeDependentControls();
}

void AvaAudioProcessorEditor::FilterSection::cacheParameterPointers(juce::AudioProcessorValueTreeState& state)
{
    typeParameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(EqlModuleProcessor::getFilterTypeParamId(filterIndex)));
    phaseParameter = dynamic_cast<juce::AudioParameterBool*>(state.getParameter(EqlModuleProcessor::getFilterPhaseParamId(filterIndex)));
    orderParameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(EqlModuleProcessor::getFilterOrderParamId(filterIndex)));
    frequencyParameter = dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(EqlModuleProcessor::getFilterFrequencyParamId(filterIndex)));
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

    const auto maximumFrequency = frequencyPresentation(getFilterType()).limits.getEnd();
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
        gainControl->setTitleText("FILTER\nDEG");
        gainControl->setValuePresentation(gainPresentation(true));

        if (gainControl->getValue() > 24.0)
            gainControl->setValue(24.0, true);
        else if (gainControl->getValue() < -24.0)
            gainControl->setValue(-24.0, true);
    }
    else
    {
        gainControl->setTitleText("FILTER\nGAIN");
        gainControl->setValuePresentation(gainPresentation(false));
    }
}

void AvaAudioProcessorEditor::FilterSection::updatePhaseForType(const bool normalizeSelection)
{
    const auto allowed = getFilterControlAvailability(getFilterType(),
        orderParameter != nullptr ? orderParameter->getIndex() : 2).phase;
    phaseIndicator->setEnabled(allowed);
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
