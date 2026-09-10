#include "Processor.h"

#include "../../crossover/ParameterAccess.h"

TlsAudioProcessor::TlsAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      valueTreeState(*this, &undoManager, "PARAMETERS", createParameterLayout())
{
    cacheParameterPointers();
    setParameterListenersEnabled(true);
}

TlsAudioProcessor::~TlsAudioProcessor()
{
    setParameterListenersEnabled(false);
}

void TlsAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    processorBank.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    syncParameters(true);
    processorBank.reset();
}

void TlsAudioProcessor::releaseResources()
{
    processorBank.releaseResources();
}

void TlsAudioProcessor::reset()
{
    processorBank.reset();
}

bool TlsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return ava::modules::dsp::supportsMatchingMonoOrStereoLayout(layouts);
}

void TlsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    ava::modules::dsp::clearOutputOnlyChannels(*this, buffer);

    syncParameters();

    processorBank.processRange(0, buffer);
}

juce::AudioProcessorEditor* TlsAudioProcessor::createEditor()
{
    return nullptr;
}

bool TlsAudioProcessor::hasEditor() const
{
    return false;
}

const juce::String TlsAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TlsAudioProcessor::acceptsMidi() const
{
    return false;
}

bool TlsAudioProcessor::producesMidi() const
{
    return false;
}

bool TlsAudioProcessor::isMidiEffect() const
{
    return false;
}

double TlsAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TlsAudioProcessor::getNumPrograms()
{
    return 1;
}

int TlsAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TlsAudioProcessor::setCurrentProgram(const int)
{
}

const juce::String TlsAudioProcessor::getProgramName(const int)
{
    return {};
}

void TlsAudioProcessor::changeProgramName(const int, const juce::String&)
{
}

void TlsAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto stateXml = valueTreeState.copyState().createXml())
        copyXmlToBinary(*stateXml, destData);
}

void TlsAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes))
    {
        if (xmlState->hasTagName(valueTreeState.state.getType()))
        {
            valueTreeState.replaceState(juce::ValueTree::fromXml(*xmlState));
            markParametersDirty();
            syncParameters(true);
        }
    }
}

juce::AudioProcessorValueTreeState& TlsAudioProcessor::getValueTreeState() noexcept
{
    return valueTreeState;
}

const juce::AudioProcessorValueTreeState& TlsAudioProcessor::getValueTreeState() const noexcept
{
    return valueTreeState;
}

juce::UndoManager& TlsAudioProcessor::getUndoManager() noexcept
{
    return undoManager;
}

const juce::UndoManager& TlsAudioProcessor::getUndoManager() const noexcept
{
    return undoManager;
}

int TlsAudioProcessor::getModuleLatencySamples() const noexcept
{
    return moduleLatencySamples.load(std::memory_order_acquire);
}

tls::dsp::ProcessorBank::RangeLatencies TlsAudioProcessor::getRangeLatencies() const noexcept
{
    return processorBank.getRangeLatencies();
}

size_t TlsAudioProcessor::ensureRangeCount(const size_t rangeCount)
{
    const auto createdRangeCount = processorBank.ensureRangeCount(rangeCount);
    processorBank.setRangeParameters(currentRangeParameters);
    return createdRangeCount;
}

size_t TlsAudioProcessor::getCreatedRangeCount() const noexcept
{
    return processorBank.getCreatedRangeCount();
}

void TlsAudioProcessor::processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
{
    processorBank.processRange(rangeIndex, buffer);
}

void TlsAudioProcessor::markParametersDirty() noexcept
{
    parametersDirty.store(true, std::memory_order_relaxed);
}

void TlsAudioProcessor::setParameterListenersEnabled(const bool enabled)
{
    ava::crossover::parameters::setRangeParameterListenersEnabled(valueTreeState,
                                                                  *this,
                                                                  tls::parameters::parameterSpecs,
                                                                  numRanges,
                                                                  enabled);
}

void TlsAudioProcessor::parameterChanged(const juce::String&, float)
{
    markParametersDirty();
}

juce::AudioProcessorValueTreeState::ParameterLayout TlsAudioProcessor::createParameterLayout()
{
    return tls::parameters::createParameterLayout();
}
