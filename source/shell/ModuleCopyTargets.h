#pragma once

#include "ModuleStackCore.h"
#include <JuceHeader.h>

// Host automation slots use stable, per-copy IDs. The VST/AU parameter
// inventory stays fixed while module instances can be created or removed.
namespace ava::module_copy_targets
{
inline constexpr const char* prefix = "module-copy-";

inline juce::String make(const int uid, const juce::String& localParameterId)
{
    return juce::String(prefix) + juce::String(uid) + ":" + localParameterId;
}

inline bool parse(const juce::String& id, int& uid, juce::String& localParameterId)
{
    if (!id.startsWith(prefix)) return false;
    const auto suffix = id.substring(static_cast<int>(juce::String(prefix).length()));
    const auto separator = suffix.indexOfChar(':');
    if (separator <= 0) return false;
    const auto digits = suffix.substring(0, separator);
    if (digits.length() > 7 || !digits.containsOnly("0123456789")) return false;
    const auto parsedUid = digits.getIntValue();
    const auto parameter = suffix.substring(separator + 1);
    if (parsedUid < ava::module_stack::originalCount || parsedUid > 1000000 || parameter.isEmpty()
        || parameter.startsWith(prefix)) return false;
    uid = parsedUid;
    localParameterId = parameter;
    return true;
}
}
