#include "WindowState.h"

#include "Processor.h"

#include <array>

namespace
{
const std::array<juce::Identifier, 7> editorWindowStateProperties
{
    AvaAudioProcessor::editorWidthStateKey,
    AvaAudioProcessor::editorHeightStateKey,
    AvaAudioProcessor::oscEnabledStateKey,
    AvaAudioProcessor::oscInputPortStateKey,
    AvaAudioProcessor::oscOutputHostStateKey,
    AvaAudioProcessor::oscOutputPortStateKey,
    AvaAudioProcessor::oscInstanceNameStateKey
};

}

void preserveEditorWindowState(juce::XmlElement& targetStateElement,
                               const juce::ValueTree& sourceState)
{
    for (const auto& propertyId : editorWindowStateProperties)
    {
        if (sourceState.hasProperty(propertyId))
            targetStateElement.setAttribute(propertyId.toString(), sourceState.getProperty(propertyId).toString());
        else
            targetStateElement.removeAttribute(propertyId.toString());
    }
}
