#pragma once

#include <JuceHeader.h>

#include <memory>

namespace shell_setup_support
{
juce::String getMixolveInfoMarkdown();
void removeOwnedChild(juce::Component& owner, std::unique_ptr<juce::Component>& child);
}
