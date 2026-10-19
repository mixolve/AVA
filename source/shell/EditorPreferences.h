#pragma once

#include <JuceHeader.h>

void preserveEditorWindowState(juce::XmlElement& targetStateElement,
                               const juce::ValueTree& sourceState);
