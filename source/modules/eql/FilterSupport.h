#pragma once

#include "Processor.h"


inline constexpr auto minimumBiquadQ = 0.025f;
inline constexpr auto maximumBiquadQ = 40.0f;
inline constexpr auto minimumBellBandwidth = 0.01f;
inline constexpr auto maximumBellBandwidth = 8.0f;
inline constexpr auto minimumVisibleFilterFrequency = 20.0f;
inline constexpr auto maximumVisibleFilterFrequency = 30000.0f;
inline constexpr auto maximumLowCutFrequency = 20000.0f;
inline constexpr auto minimumDesignFilterFrequency = 2.0;
inline constexpr auto highFrequencyExtensionStart = 20000.0;
inline constexpr auto lowFrequencyExtensionEnd = 25.0;
inline constexpr auto defaultFilterFrequencyHz = 632.46f;
inline constexpr auto defaultFilterBandwidthOctaves = 1.0f;
inline constexpr auto defaultFilterSlopeDbPerOct = EqlModuleProcessor::fixedSlopeDbPerOct;
inline constexpr auto nyquistSafetyFactor = 0.98;
inline constexpr auto flatTiltStageCount = 16;
inline const juce::StringArray filterTypeChoices { "LCT", "LSH", "BEL", "FTL", "HSH", "HCT", "VOL" };
inline const juce::StringArray filterPlaceChoices { "LR", "LL", "RR", "MM", "SS", "PHS", "PHL", "PHR" };

struct ShelfSlopeBlend
{
    int lowerOrder = 1;
    int upperOrder = 1;
    double blend = 0.0;
};


juce::String formatDecibelValue(float value);
juce::String formatFrequencyValue(float value);
juce::String formatBandwidthValue(float value);
juce::String makeFilterParameterId(const char* suffix, int filterIndex);
juce::StringArray getOrderDisplayChoicesForType(EqlModuleProcessor::FilterType type) noexcept;
int clampActiveFilterCount(int filterCount);
juce::String filterTypeDisplayPrefix(EqlModuleProcessor::FilterType type);

bool isShelfFilterType(EqlModuleProcessor::FilterType type) noexcept;
bool isCutFilterType(EqlModuleProcessor::FilterType type) noexcept;
bool isTiltFilterType(EqlModuleProcessor::FilterType type) noexcept;
bool isVolumeFilterType(EqlModuleProcessor::FilterType type) noexcept;
bool isPhasePlaceChoice(int choiceIndex) noexcept;
ShelfSlopeBlend mapBellSlopeToBlend(double slope) noexcept;
ShelfSlopeBlend mapShelfSlopeToBlend(double slope) noexcept;
ShelfSlopeBlend mapCutSlopeToBlend(EqlModuleProcessor::FilterType type, double slope) noexcept;
double computeButterworthStageQ(int biquadStageIndex, int order) noexcept;
double computeDesignFrequency(double displayedFrequency, double sampleRate) noexcept;
double mapBandwidthToShelfShape(double octaveBandwidth) noexcept;
