#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <limits>
#include <optional>

namespace ava::modules::state
{
template <typename Order>
inline bool isPermutation(const Order& order, const int count) noexcept
{
    if (count < 0 || static_cast<int>(order.size()) < count) return false;
    for (int index = 0; index < count; ++index)
    {
        const auto value = static_cast<int>(order[static_cast<size_t>(index)]);
        if (!juce::isPositiveAndBelow(value, count)) return false;
        for (int previous = 0; previous < index; ++previous)
            if (order[static_cast<size_t>(previous)] == order[static_cast<size_t>(index)]) return false;
    }
    return true;
}

inline std::optional<double> parseExactFiniteNumber(const juce::var& value)
{
    const auto valueText = value.toString();

    if (valueText.isEmpty())
        return std::nullopt;

    auto index = 0;

    if (valueText[index] == '-')
    {
        if (++index >= valueText.length())
            return std::nullopt;
    }

    const auto integerStart = index;

    while (index < valueText.length() && valueText[index] >= '0' && valueText[index] <= '9')
        ++index;

    if (index == integerStart)
        return std::nullopt;

    if (index - integerStart > 1 && valueText[integerStart] == '0')
        return std::nullopt;

    if (index < valueText.length() && valueText[index] == '.')
    {
        const auto fractionStart = ++index;

        while (index < valueText.length() && valueText[index] >= '0' && valueText[index] <= '9')
            ++index;

        if (index == fractionStart)
            return std::nullopt;
    }

    if (index < valueText.length() && (valueText[index] == 'e' || valueText[index] == 'E'))
    {
        ++index;

        if (index < valueText.length() && (valueText[index] == '+' || valueText[index] == '-'))
            ++index;

        const auto exponentStart = index;

        while (index < valueText.length() && valueText[index] >= '0' && valueText[index] <= '9')
            ++index;

        if (index == exponentStart)
            return std::nullopt;
    }

    if (index != valueText.length())
        return std::nullopt;

    // Syntax has already been checked in full. JUCE parses with the C locale,
    // independent of the host's decimal separator.
    const auto parsedValue = valueText.getDoubleValue();

    if (! std::isfinite(parsedValue))
        return std::nullopt;

    return parsedValue;
}

inline std::optional<float> readParameterPlainValue(const juce::ValueTree& state,
                                                    const juce::String& parameterId)
{
    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        const auto child = state.getChild(childIndex);

        if (! child.hasType("PARAM") || child.getProperty("id").toString() != parameterId)
            continue;

        const auto value = parseExactFiniteNumber(child.getProperty("value"));

        if (! value.has_value()
            || std::abs(*value) > static_cast<double>(std::numeric_limits<float>::max()))
            return std::nullopt;

        return static_cast<float>(*value);
    }

    return std::nullopt;
}

// Validate the current set of parameter IDs exactly. Old IDs and incomplete
// layouts are not converted or silently substituted with current defaults.
inline bool hasValidParameterState(const juce::ValueTree& candidate,
                                   juce::AudioProcessorValueTreeState& parameters,
                                   const juce::Identifier& allowedExtraChildType = {})
{
    if (!candidate.isValid() || candidate.getType() != parameters.state.getType()) return false;

    juce::HashMap<juce::String, bool> expected;
    for (const auto child : parameters.copyState())
        if (child.hasType("PARAM"))
        {
            const auto id = child.getProperty("id").toString();
            if (parameters.getParameter(id) != nullptr) expected.set(id, true);
        }

    juce::HashMap<juce::String, bool> received;
    int extraChildren = 0;
    for (const auto child : candidate)
    {
        if (!child.hasType("PARAM"))
        {
            if (allowedExtraChildType.toString().isEmpty()
                || child.getType() != allowedExtraChildType || ++extraChildren > 1)
                return false;
            continue;
        }
        const auto id = child.getProperty("id").toString();
        if (!expected.contains(id)) return false;
        const auto value = parseExactFiniteNumber(child.getProperty("value"));
        const auto* parameter = parameters.getParameter(id);
        if (child.getNumChildren() != 0 || id.isEmpty() || received.contains(id)
            || parameter == nullptr || !value.has_value()) return false;
        const auto range = parameter->getNormalisableRange();
        if (*value < static_cast<double>(range.start) - 1.0e-5
            || *value > static_cast<double>(range.end) + 1.0e-5)
            return false;
        received.set(id, true);
    }
    if (received.size() != expected.size()) return false;
    return allowedExtraChildType.toString().isEmpty() || extraChildren == 1;
}

}
