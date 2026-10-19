#include "EditorPreferences.h"
#include "AnimationState.h"

#include "Processor.h"

#include <array>

namespace
{
const std::array<juce::Identifier, 6> editorWindowStateProperties
{
    AvaAudioProcessor::editorWidthStateKey,
    AvaAudioProcessor::editorHeightStateKey,
    AvaAudioProcessor::oscEnabledStateKey,
    AvaAudioProcessor::oscInputPortStateKey,
    AvaAudioProcessor::oscOutputHostStateKey,
    AvaAudioProcessor::oscOutputPortStateKey
};

}

void preserveEditorWindowState(juce::XmlElement& targetStateElement,
                               const juce::ValueTree& sourceState)
{
    preserveAnimationSettings(targetStateElement, sourceState);
    for (const auto& propertyId : editorWindowStateProperties)
    {
        if (sourceState.hasProperty(propertyId))
            targetStateElement.setAttribute(propertyId.toString(), sourceState.getProperty(propertyId).toString());
        else
            targetStateElement.removeAttribute(propertyId.toString());
    }
}
