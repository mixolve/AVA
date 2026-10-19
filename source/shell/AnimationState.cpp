#include "AnimationState.h"
#include "../modules/shared/StateValidation.h"
#include <cmath>

juce::String animationBandProperty(const size_t band)
{
    return band == 0 ? "anim-top" : "anim-bottom";
}

juce::String animationModeProperty(const size_t band, const int mode, const char* property)
{
    return animationBandProperty(band) + "-" + animationModeName(mode) + "-" + property;
}

namespace
{
bool isInRange(const juce::var& value, double low, double high, bool integer = false)
{
    const auto parsed = ava::modules::state::parseExactFiniteNumber(value);
    return parsed.has_value() && *parsed >= low && *parsed <= high
        && (!integer || std::floor(*parsed) == *parsed);
}
}

std::optional<bool> validateAnimationProperty(const juce::Identifier& property, const juce::var& value)
{
    if (property == juce::Identifier("anim-same") || property == juce::Identifier("anim-sync"))
        return isInRange(value, 0, 1, true);
    for (size_t band = 0; band < 2; ++band)
    {
        if (property == juce::Identifier(animationBandProperty(band)))
            return isInRange(value, 0, animationModeCount, true);
        if (property == juce::Identifier(animationModeProperty(band, customTextAnimationMode, "text")))
            return value.isString();
        for (int mode = 1; mode <= animationModeCount; ++mode)
        {
            if (property == juce::Identifier(animationModeProperty(band, mode, "color")))
                return isInRange(value, 0, static_cast<double>(animationColours.size() - 1), true);
            if (mode >= 3 && property == juce::Identifier(animationModeProperty(band, mode, "moving")))
                return isInRange(value, 0, 1, true);
            if (mode <= 2 && property == juce::Identifier(animationModeProperty(band, mode, "crop-y")))
                return isInRange(value, 0, 1);
            for (const auto& field : animationFields)
                if (field.supports(mode) && property == juce::Identifier(animationModeProperty(band, mode, field.property)))
                    return isInRange(value, field.lowerBound(mode), field.upperBound(mode), field.decimals == 0);
        }
    }
    return std::nullopt;
}

void loadAnimationSettings(AnimationSettings& settings, const juce::ValueTree& state)
{
    settings.same.store(static_cast<bool>(state.getProperty("anim-same", false)));
    settings.sync.store(static_cast<bool>(state.getProperty("anim-sync", false)));
    for (size_t bandIndex = 0; bandIndex < 2; ++bandIndex)
    {
        auto& band = bandIndex == 0 ? settings.top : settings.bottom;
        band.animation.store(juce::jlimit(0, animationModeCount, static_cast<int>(state.getProperty(animationBandProperty(bandIndex), 1))));
        band.setCustomText(state.getProperty(animationModeProperty(bandIndex, customTextAnimationMode, "text"), juce::String {}).toString());
        for (int modeId = 1; modeId <= animationModeCount; ++modeId)
        {
            auto& mode = band.modes[static_cast<size_t>(modeId - 1)];
            const auto lastColour = static_cast<int>(animationColours.size() - 1);
            mode.colour.store(juce::jlimit(0, lastColour, static_cast<int>(state.getProperty(animationModeProperty(bandIndex, modeId, "color"), lastColour))));
            if (modeId >= 3) mode.moving.store(static_cast<bool>(state.getProperty(animationModeProperty(bandIndex, modeId, "moving"), true)));
            if (modeId <= 2) mode.cropY.store(juce::jlimit(0.f, 1.f, static_cast<float>(state.getProperty(animationModeProperty(bandIndex, modeId, "crop-y"), 0.f))));
            for (const auto& field : animationFields)
                if (field.supports(modeId))
                    (mode.*field.value).store(static_cast<float>(juce::jlimit(field.lowerBound(modeId), field.upperBound(modeId),
                        static_cast<double>(state.getProperty(animationModeProperty(bandIndex, modeId, field.property), field.defaultValue(modeId))))));
        }
    }
}

void preserveAnimationSettings(juce::XmlElement& target, const juce::ValueTree& state)
{
    const auto preserve = [&target, &state](const juce::String& name, bool uppercase = false)
    {
        if (state.hasProperty(name))
        {
            const auto text = state.getProperty(name).toString();
            target.setAttribute(name, uppercase ? uppercaseText(text) : text);
        }
        else target.removeAttribute(name);
    };
    preserve("anim-same");
    preserve("anim-sync");
    for (size_t band = 0; band < 2; ++band)
    {
        preserve(animationBandProperty(band));
        preserve(animationModeProperty(band, customTextAnimationMode, "text"), true);
        for (int mode = 1; mode <= animationModeCount; ++mode)
        {
            preserve(animationModeProperty(band, mode, "color"));
            if (mode >= 3) preserve(animationModeProperty(band, mode, "moving"));
            if (mode <= 2) preserve(animationModeProperty(band, mode, "crop-y"));
            for (const auto& field : animationFields)
                if (field.supports(mode)) preserve(animationModeProperty(band, mode, field.property));
        }
    }
}
