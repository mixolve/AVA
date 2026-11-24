#pragma once

#include <JuceHeader.h>
#include <cstddef>

// A MODULES tile refers to one module slot in one crossover band.  Original
// UIDs recur in every band, so both coordinates are required for host slots.
namespace ava::module_slot_targets
{
inline constexpr auto prefix = "module-slot-b";

inline juce::String make(const size_t band, const int uid)
{
    return juce::String(prefix) + juce::String(int(band) + 1)
        + "-u" + juce::String(uid) + "-bypass";
}

inline bool parse(const juce::String& id, size_t& band, int& uid)
{
    if (!id.startsWith(prefix) || !id.endsWith("-bypass")) return false;
    const auto middle = id.substring(int(juce::String(prefix).length()), id.length() - 7);
    const auto separator = middle.indexOf("-u");
    if (separator <= 0) return false;
    const auto bandDigits = middle.substring(0, separator);
    const auto uidDigits = middle.substring(separator + 2);
    if (bandDigits.isEmpty() || !bandDigits.containsOnly("0123456789")
        || uidDigits.isEmpty() || uidDigits == "-" || uidDigits.length() > 8) return false;
    const auto digitPart = uidDigits.startsWithChar('-') ? uidDigits.substring(1) : uidDigits;
    if (!digitPart.containsOnly("0123456789")) return false;
    const auto parsedBand = bandDigits.getIntValue();
    const auto parsedUid = uidDigits.getIntValue();
    if (parsedBand <= 0 || parsedBand > 8 || parsedUid < -1000000 || parsedUid > 1000000
        || make(size_t(parsedBand - 1), parsedUid) != id) return false;
    band = size_t(parsedBand - 1);
    uid = parsedUid;
    return true;
}
}
