#pragma once

#include <JuceHeader.h>
#include "ParameterFormatting.h"

#include <functional>
#include <span>
#include <memory>
#include <utility>

namespace ava::modules::parameters
{
struct ControlMetadata
{
    const char* auxiliarySuffix = "";
    const char* auxiliaryLabel = "";
    bool gainWithMute = false;
    bool hostSlot = true;
    const char* modeSuffix = "";
    const char* noteSuffix = "";
};

struct ControlPolicy
{
    ControlMetadata metadata;
    bool visible = true, enabled = true;
    int cellSpan = 1;
    juce::StringArray choices;
    NumericPresentation presentation;
    const char* title = nullptr;
    std::span<const char* const> dependencies;
};

inline std::unique_ptr<juce::RangedAudioParameter> makeBoolParameter(
    const juce::String& id, const juce::String& name, const bool defaultValue)
{
    return std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { id, 1 }, name, defaultValue,
        juce::AudioParameterBoolAttributes().withAutomatable(false));
}

inline std::unique_ptr<juce::RangedAudioParameter> makeChoiceParameter(
    const juce::String& id, const juce::String& name,
    const juce::StringArray& choices, const int defaultIndex)
{
    return std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { id, 1 }, name, choices, defaultIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false));
}

class ValueParameter final : public juce::AudioParameterFloat
{
public:
    using Formatter = std::function<ValueText(float)>;
    ValueParameter(const juce::String& parameterId, const juce::String& parameterName,
        const juce::NormalisableRange<float>& plainRange, float defaultValue, Formatter formatter,
        const juce::String& unitLabel, bool noteInput,
        juce::AudioParameterFloatAttributes attributes, double textScale = 1.0)
        : juce::AudioParameterFloat(juce::ParameterID { parameterId, 1 }, parameterName, plainRange, defaultValue,
            attributes.withLabel(unitLabel)
                .withStringFromValueFunction([formatter] (float value, int) { return formatter(value).text; })
                .withValueFromStringFunction([formatter, plainRange, noteInput, defaultValue, textScale] (const juce::String& text)
                    {
                        const auto value = parse(text, plainRange, formatter, noteInput) / textScale;
                        return std::isfinite(value) ? float(juce::jlimit(double(plainRange.start), double(plainRange.end), value)) : defaultValue;
                    })),
          valueFormatter(std::move(formatter)), notesAllowed(noteInput) {}

    double parsePlainValue(const juce::String& text, const NumericPresentation& presentation) const
    {
        if (!presentation.isIdentity())
        {
            const auto value = parseNumericInput(text);
            return std::isfinite(value) ? presentation.toPlain(value) : value;
        }
        return parse(text, getNormalisableRange(), valueFormatter, notesAllowed);
    }
    juce::String formatUiValue(double value, double minimum, double maximum, int decimals,
                               bool editing, const NumericPresentation& presentation) const
    {
        if (presentation.isIdentity())
        {
            const auto resolved = valueFormatter(float(value));
            if (resolved.isName && (!editing || resolved.keepNameWhenEditing)) return resolved.text;
        }
        auto text = formatRangeDecimalValue(presentation.toDisplay(value), presentation.toDisplay(minimum),
                                            presentation.toDisplay(maximum), decimals);
        return text;
    }
private:
    static double parse(const juce::String& text, const juce::NormalisableRange<float>& plainRange,
                        const Formatter& formatter, bool noteInput)
    {
        for (const auto candidate : { plainRange.start, plainRange.end, juce::jlimit(plainRange.start, plainRange.end, 0.0f) })
        {
            const auto named = formatter(candidate);
            if (named.isName && text.trim().equalsIgnoreCase(named.text)) return candidate;
        }
        return noteInput ? parseFrequencyInput(text) : parseNumericInput(text);
    }
    Formatter valueFormatter;
    bool notesAllowed = false;
};

inline std::unique_ptr<juce::RangedAudioParameter> makeFloatParameter(
    const juce::String& id, const juce::String& name,
    const juce::NormalisableRange<float>& range, const float defaultValue,
    ValueParameter::Formatter formatter, const juce::String& label = {}, bool noteInput = false,
    juce::AudioParameterFloatAttributes attributes = juce::AudioParameterFloatAttributes().withAutomatable(false),
    double textScale = 1.0)
{
    return std::make_unique<ValueParameter>(id, name, range, defaultValue, std::move(formatter), label, noteInput, attributes, textScale);
}
}
