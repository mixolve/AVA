#include "EditorSetup.h"

namespace shell_setup_support
{

juce::String getMixolveInfoMarkdown()
{
    return juce::String::fromUTF8(BinaryData::about_md, BinaryData::about_mdSize);
}

void removeOwnedChild(juce::Component& owner, std::unique_ptr<juce::Component>& child)
{
    if (child == nullptr)
        return;

    owner.removeChildComponent(child.get());
    child.reset();
}
}
