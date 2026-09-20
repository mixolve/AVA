#pragma once

#include "Processor.h"

#include <memory>

inline constexpr auto filterPresetsRootTag = "FILTER_PRESETS";
inline constexpr auto presetTag = "PRESET";
inline constexpr auto presetStorageVendorFolder = "mixolve";
inline constexpr auto presetStorageProductFolder = "ava";
inline constexpr auto eqlPresetStorageModuleFolder = "eql";
inline constexpr auto presetStorageRootFolder = "presets";

std::unique_ptr<juce::XmlElement> loadFilterPresetsXml();
std::unique_ptr<juce::XmlElement> createEmptyFilterPresetsXml();
bool writeFilterPresetsXml(const juce::XmlElement& rootElement);
juce::XmlElement* findPresetElement(juce::XmlElement& rootElement, const juce::String& presetName);
std::unique_ptr<juce::XmlElement> createSerializableStateXml(juce::AudioProcessorValueTreeState& parameters,
                                                             int activeFilterCount);
std::unique_ptr<juce::XmlElement> createSerializableStateXml(EqlModuleProcessor& processor);
