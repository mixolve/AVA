#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <optional>
#include <limits>

namespace ava::modules::parameters
{
// A named value is explicit model data, never inferred by searching display text.
struct ValueText
{
    juce::String text;
    bool isName = false, keepNameWhenEditing = false;
    ValueText(juce::String numericText) : text(std::move(numericText)) {}
    static ValueText named(juce::String text, bool keepWhenEditing = false)
    {
        ValueText result(std::move(text));
        result.isName = true; result.keepNameWhenEditing = keepWhenEditing;
        return result;
    }
};

struct NumericPresentation
{
    double scale = 1.0;
    juce::Range<double> limits;
    enum class Mapping { linear, rateToSemitones };
    Mapping mapping = Mapping::linear;
    bool operator==(const NumericPresentation& other) const noexcept {
        return mapping == other.mapping && juce::approximatelyEqual(scale, other.scale)
            && juce::approximatelyEqual(limits.getStart(), other.limits.getStart())
            && juce::approximatelyEqual(limits.getEnd(), other.limits.getEnd());
    }
    bool isIdentity() const noexcept { return mapping == Mapping::linear && juce::approximatelyEqual(scale, 1.0) && limits.isEmpty(); }
    double toDisplay(double value) const {
        const auto result = mapping == Mapping::rateToSemitones
            ? 12.0 * std::log2(std::max(value, std::numeric_limits<double>::min())) : value * scale;
        return limits.isEmpty() ? result : limits.clipValue(result);
    }
    double toPlain(double value) const {
        const auto display = limits.isEmpty() ? value : limits.clipValue(value);
        return mapping == Mapping::rateToSemitones ? std::exp2(display / 12.0) : display / scale;
    }
};

inline bool tryParseNoteFrequency(const juce::String& text, double& frequency)
{
    auto trimmed = text.trim().removeCharacters(" \t");

    if (trimmed.isEmpty())
        return false;

    const auto firstChar = static_cast<juce::juce_wchar>(juce::CharacterFunctions::toLowerCase(trimmed[0]));

    int semitone = 0;

    switch (firstChar)
    {
        case 'c': semitone = 0; break;
        case 'd': semitone = 2; break;
        case 'e': semitone = 4; break;
        case 'f': semitone = 5; break;
        case 'g': semitone = 7; break;
        case 'a': semitone = 9; break;
        case 'b': semitone = 11; break;
        default: return false;
    }

    int position = 1;

    if (position < trimmed.length())
    {
        const auto accidental = trimmed[position];

        if (accidental == '#' || accidental == 0x266f)
        {
            ++semitone;
            ++position;
        }
        else if (accidental == 'b' || accidental == 0x266d)
        {
            --semitone;
            ++position;
        }
    }

    if (position >= trimmed.length())
        return false;

    const auto octaveText = trimmed.substring(position);

    const auto digits = octaveText.startsWithChar('+') || octaveText.startsWithChar('-')
        ? octaveText.substring(1) : octaveText;
    if (digits.isEmpty() || !digits.containsOnly("0123456789")) return false;

    if (octaveText.length() > 5) return false;
    const auto octave = octaveText.getIntValue();
    if (octave < -100 || octave > 100) return false;
    const auto midiNote = ((octave + 1) * 12) + semitone;
    frequency = 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
    return std::isfinite(frequency) && frequency > 0.0;
}

inline double parseNumericInput(const juce::String& text)
{
    const auto input = text.trim().removeCharacters(" \t").upToFirstOccurrenceOf(":", false, false).replaceCharacter(',', '.');
    const auto invalid = std::numeric_limits<double>::quiet_NaN();
    int end = 0, digits = 0;
    if (input[end] == '+' || input[end] == '-') ++end;
    const auto isDigit = [&] (int index) { return input[index] >= '0' && input[index] <= '9'; };
    while (isDigit(end)) { ++end; ++digits; }
    if (input[end] == '.')
    {
        ++end;
        while (isDigit(end)) { ++end; ++digits; }
    }
    if (digits == 0) return invalid;
    if (input[end] == 'e' || input[end] == 'E')
    {
        ++end;
        if (input[end] == '+' || input[end] == '-') ++end;
        const auto exponentStart = end;
        while (isDigit(end)) ++end;
        if (end == exponentStart) return invalid;
    }
    const auto suffix = input.substring(end);
    if (suffix.startsWithChar('.') || suffix.startsWithChar('+') || suffix.startsWithChar('-')) return invalid;
    auto value = input.substring(0, end).getDoubleValue();
    if (suffix.startsWithIgnoreCase("k") || suffix.startsWithIgnoreCase(juce::String::fromUTF8("к"))) value *= 1000.0;
    return std::isfinite(value) ? value : invalid;
}

inline double parseFrequencyInput(const juce::String& text)
{
    double noteFrequency = 0.0;

    if (tryParseNoteFrequency(text, noteFrequency))
        return noteFrequency;

    return parseNumericInput(text);
}

inline juce::String formatFixedDecimalValue(const double value, const int decimalPlaces)
{
    const auto places = juce::jmax(0, decimalPlaces);
    const auto scale = std::pow(10.0, static_cast<double>(places));
    auto roundedValue = std::round(value * scale) / scale;

    if (std::abs(roundedValue) < (0.5 / scale))
        roundedValue = 0.0;

    return juce::String(roundedValue, places);
}

// The displayed range fixes the sign column and integer width. Rounding the
// endpoints first also reserves a carry digit (e.g. 99.999 shown to two places).
inline juce::String formatRangeDecimalValue(const double value, const double minimum,
                                           const double maximum, const int decimalPlaces)
{
    const auto places = juce::jmax(0, decimalPlaces);
    auto text = formatFixedDecimalValue(value, places);
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum)) return text;
    const auto scale = std::pow(10.0, static_cast<double>(places));
    const auto extent = juce::jmax(std::abs(minimum), std::abs(maximum));
    const auto rounded = std::round(extent * scale) / scale;
    auto whole = std::isfinite(rounded) ? rounded : extent;
    int integerPlaces = 1;
    while (whole >= 10.0) { whole = std::floor(whole / 10.0); ++integerPlaces; }
    const auto negative = text.startsWithChar('-');
    if (negative) text = text.substring(1);
    text = text.paddedLeft('0', integerPlaces + (places > 0 ? places + 1 : 0));
    if (negative) return "-" + text;
    return juce::jmin(minimum, maximum) < 0.0 ? "+" + text : text;
}

}
