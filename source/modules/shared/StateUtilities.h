#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>

namespace ava::modules::state
{
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

    while (index < valueText.length() && juce::CharacterFunctions::isDigit(valueText[index]))
        ++index;

    if (index == integerStart)
        return std::nullopt;

    if (index - integerStart > 1 && valueText[integerStart] == '0')
        return std::nullopt;

    if (index < valueText.length() && valueText[index] == '.')
    {
        const auto fractionStart = ++index;

        while (index < valueText.length() && juce::CharacterFunctions::isDigit(valueText[index]))
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

        while (index < valueText.length() && juce::CharacterFunctions::isDigit(valueText[index]))
            ++index;

        if (index == exponentStart)
            return std::nullopt;
    }

    if (index != valueText.length())
        return std::nullopt;

    const auto* valueStart = valueText.toRawUTF8();
    char* valueEnd = nullptr;
    const auto parsedValue = std::strtod(valueStart, &valueEnd);

    if (valueEnd == valueStart
        || *valueEnd != '\0'
        || ! std::isfinite(parsedValue))
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

        if (! value.has_value())
            return std::nullopt;

        return static_cast<float>(*value);
    }

    return std::nullopt;
}

inline bool hasExactParameterState(const juce::ValueTree& candidate,
                                   juce::AudioProcessorValueTreeState& parameters,
                                   const juce::Identifier& allowedExtraChildType = {})
{
    if (! candidate.isValid() || candidate.getType() != parameters.state.getType())
        return false;

    const auto current = parameters.copyState();
    juce::StringArray expectedParameterIds;

    for (int childIndex = 0; childIndex < current.getNumChildren(); ++childIndex)
    {
        const auto child = current.getChild(childIndex);

        if (! child.hasType("PARAM"))
            continue;

        const auto parameterId = child.getProperty("id").toString();

        if (parameterId.isEmpty() || expectedParameterIds.contains(parameterId))
            return false;

        expectedParameterIds.add(parameterId);
    }

    juce::StringArray restoredParameterIds;
    auto extraChildCount = 0;

    for (int childIndex = 0; childIndex < candidate.getNumChildren(); ++childIndex)
    {
        const auto child = candidate.getChild(childIndex);

        if (! child.hasType("PARAM"))
        {
            if (allowedExtraChildType.toString().isEmpty() || child.getType() != allowedExtraChildType || ++extraChildCount > 1)
                return false;

            continue;
        }

        if (child.getNumChildren() != 0
            || child.getNumProperties() != 2
            || ! child.hasProperty("id")
            || ! child.hasProperty("value"))
            return false;

        const auto parameterId = child.getProperty("id").toString();

        auto* parameter = parameters.getParameter(parameterId);
        const auto plainValue = parseExactFiniteNumber(child.getProperty("value"));

        if (parameterId.isEmpty()
            || parameter == nullptr
            || restoredParameterIds.contains(parameterId)
            || ! plainValue.has_value())
            return false;

        const auto& range = parameter->getNormalisableRange();
        const auto boundaryTolerance = juce::jmax(1.0e-6,
                                                  static_cast<double>(range.end - range.start) * 1.0e-6);

        if (*plainValue < static_cast<double>(range.start) - boundaryTolerance
            || *plainValue > static_cast<double>(range.end) + boundaryTolerance)
            return false;

        if (range.interval > 0.0f)
        {
            const auto snappedValue = static_cast<double>(range.snapToLegalValue(static_cast<float>(*plainValue)));
            const auto floatRoundingTolerance = static_cast<double>(std::numeric_limits<float>::epsilon())
                * juce::jmax(1.0, std::abs(snappedValue)) * 2.0;
            const auto gridTolerance = juce::jmax(1.0e-6,
                                                  static_cast<double>(range.interval) * 1.0e-4,
                                                  floatRoundingTolerance);

            if (std::abs(snappedValue - *plainValue) > gridTolerance)
                return false;
        }

        restoredParameterIds.add(parameterId);
    }

    if (! allowedExtraChildType.toString().isEmpty() && extraChildCount != 1)
        return false;

    if (restoredParameterIds.size() != expectedParameterIds.size())
        return false;

    for (const auto& parameterId : expectedParameterIds)
        if (! restoredParameterIds.contains(parameterId))
            return false;

    return true;
}

}
