#pragma once

#include "Processor.h"

#include <memory>

inline constexpr auto filterPresetsRootTag = "FILTER_PRESETS";
inline constexpr auto presetTag = "PRESET";
inline constexpr auto presetStorageVendorFolder = "mixolve";
inline constexpr auto presetStorageProductFolder = "ava";
inline constexpr auto eqlPresetStorageModuleFolder = "eql";
inline constexpr auto presetStorageRootFolder = "presets";

// One transaction spans read-modify-write across plugin instances and processes.
class PresetCatalogTransaction {
public:
    PresetCatalogTransaction();
    ~PresetCatalogTransaction();
    explicit operator bool() const { return acquired; }
private:
    juce::ScopedLock threadLock;
    bool acquired = false;
};
juce::String getFilterPresetCatalogStamp();
std::unique_ptr<juce::XmlElement> loadFilterPresetsXml();
std::unique_ptr<juce::XmlElement> createEmptyFilterPresetsXml();
bool writeFilterPresetsXml(const juce::XmlElement& rootElement);
juce::XmlElement* findPresetElement(juce::XmlElement& rootElement, const juce::String& presetName);
juce::ValueTree copySerializableState(EqlModuleProcessor& processor);
