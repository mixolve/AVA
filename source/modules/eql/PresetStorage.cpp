#include "PresetStorage.h"

#include <array>
#include <cstdint>

static bool writeXmlToFile(const juce::XmlElement& element, const juce::File& file);

static juce::File getDocumentsPresetStorageDirectory()
{
    auto directory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile(presetStorageVendorFolder)
                         .getChildFile(presetStorageProductFolder);

    directory = directory.getChildFile(eqlPresetStorageModuleFolder)
                         .getChildFile(presetStorageRootFolder);
    directory.createDirectory();

    return directory;
}

static juce::File getFilterPresetsDirectory()
{
    auto directory = getDocumentsPresetStorageDirectory();
    directory.createDirectory();
    return directory;
}

static juce::String makePresetFileStem(const juce::String& presetName)
{
    const auto trimmedName = presetName.trim();
    const auto utf8 = trimmedName.toRawUTF8();
    auto hash = std::uint64_t { 14695981039346656037ull };

    for (const auto* byte = reinterpret_cast<const unsigned char*>(utf8);
         byte != nullptr && *byte != 0;
         ++byte)
    {
        hash ^= static_cast<std::uint64_t>(*byte);
        hash *= std::uint64_t { 1099511628211ull };
    }

    auto readablePrefix = juce::File::createLegalFileName(trimmedName).trim();
    readablePrefix = readablePrefix.substring(0, 48);

    if (readablePrefix.isEmpty())
        readablePrefix = "preset";

    std::array<char, 17> hashText {};
    constexpr char hexDigits[] = "0123456789abcdef";

    for (int digit = 15; digit >= 0; --digit)
    {
        hashText[static_cast<size_t>(digit)] = hexDigits[hash & 0x0fu];
        hash >>= 4u;
    }

    return readablePrefix + "-" + juce::String(hashText.data());
}

static juce::File getPresetFileForName(const juce::File& directory, const juce::String& presetName)
{
    return directory.getChildFile(makePresetFileStem(presetName) + ".xml");
}

static bool containsFileNameIgnoreCase(const juce::StringArray& fileNames, const juce::String& fileName)
{
    for (const auto& candidate : fileNames)
    {
        if (candidate.equalsIgnoreCase(fileName))
            return true;
    }

    return false;
}

static bool writeXmlToFile(const juce::XmlElement& element, const juce::File& file)
{
    juce::TemporaryFile temporaryFile(file);

    if (auto outputStream = temporaryFile.getFile().createOutputStream())
    {
        outputStream->setPosition(0);
        outputStream->truncate();
        element.writeTo(*outputStream, {});
        outputStream->flush();
        return temporaryFile.overwriteTargetFileWithTemporary();
    }

    return false;
}

static std::unique_ptr<juce::XmlElement> loadXmlFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    return juce::XmlDocument::parse(file);
}

static juce::StringArray collectPresetFileNames(const juce::XmlElement& rootElement,
                                                 const juce::File& directory,
                                                 const juce::String& presetElementTag)
{
    juce::StringArray fileNames;

    for (auto* child : rootElement.getChildIterator())
    {
        if (! child->hasTagName(presetElementTag))
            continue;

        const auto presetName = child->getStringAttribute("name").trim();

        if (presetName.isNotEmpty())
            fileNames.addIfNotAlreadyThere(getPresetFileForName(directory, presetName).getFileName());
    }

    return fileNames;
}

static bool writePresetCollectionToDirectory(const juce::File& directory,
                                             const juce::XmlElement& rootElement,
                                             const juce::String& presetElementTag)
{
    directory.createDirectory();

    const auto expectedFileNames = collectPresetFileNames(rootElement, directory, presetElementTag);
    juce::StringArray writtenFileNames;

    for (auto* child : rootElement.getChildIterator())
    {
        if (! child->hasTagName(presetElementTag))
            continue;

        const auto presetName = child->getStringAttribute("name").trim();

        if (presetName.isEmpty())
            continue;

        const auto presetFile = getPresetFileForName(directory, presetName);
        const auto presetFileName = presetFile.getFileName();

        if (containsFileNameIgnoreCase(writtenFileNames, presetFileName))
            return false;

        writtenFileNames.add(presetFileName);

        auto presetCopy = std::make_unique<juce::XmlElement>(*child);
        presetCopy->removeAttribute(EqlModuleProcessor::filterPresetSelectedStateKey);

        if (auto* stateElement = presetCopy->getChildByName("eql_state"))
        {
            stateElement->removeAttribute(EqlModuleProcessor::filterPresetSelectedStateKey);
        }

        if (! writeXmlToFile(*presetCopy, presetFile))
            return false;
    }

    juce::Array<juce::File> existingFiles;
    directory.findChildFiles(existingFiles, juce::File::findFiles, false, "*.xml");

    for (const auto& existingFile : existingFiles)
    {
        if (! containsFileNameIgnoreCase(expectedFileNames, existingFile.getFileName()))
            existingFile.deleteFile();
    }

    return true;
}

static std::unique_ptr<juce::XmlElement> loadPresetCollectionFromDirectory(const juce::File& directory,
                                                                           const juce::String& rootTag,
                                                                           const juce::String& presetElementTag)
{
    juce::Array<juce::File> presetFiles;
    directory.findChildFiles(presetFiles, juce::File::findFiles, false, "*.xml");

    if (presetFiles.isEmpty())
        return {};

    auto collection = std::make_unique<juce::XmlElement>(rootTag);

    for (const auto& presetFile : presetFiles)
    {
        auto presetXml = loadXmlFile(presetFile);

        if (presetXml == nullptr || ! presetXml->hasTagName(presetElementTag))
            continue;

        const auto presetName = presetXml->getStringAttribute("name").trim();

        if (presetName.isEmpty()
            || presetFile.getFileName() != getPresetFileForName(directory, presetName).getFileName()
            || findPresetElement(*collection, presetName) != nullptr)
            continue;

        collection->addChildElement(presetXml.release());
    }

    if (collection->getNumChildElements() == 0)
        return {};

    return collection;
}

juce::XmlElement* findPresetElement(juce::XmlElement& rootElement, const juce::String& presetName)
{
    for (auto* child : rootElement.getChildIterator())
    {
        if (child->hasTagName(presetTag) && child->getStringAttribute("name").equalsIgnoreCase(presetName))
            return child;
    }

    return nullptr;
}

std::unique_ptr<juce::XmlElement> loadFilterPresetsXml()
{
    return loadPresetCollectionFromDirectory(getFilterPresetsDirectory(),
                                             filterPresetsRootTag,
                                             presetTag);
}

std::unique_ptr<juce::XmlElement> createEmptyFilterPresetsXml()
{
    return std::make_unique<juce::XmlElement>(filterPresetsRootTag);
}

bool writeFilterPresetsXml(const juce::XmlElement& rootElement)
{
    const auto filterPresetsDirectory = getFilterPresetsDirectory();

    if (! writePresetCollectionToDirectory(filterPresetsDirectory, rootElement, presetTag))
        return false;


    return true;
}

std::unique_ptr<juce::XmlElement> createSerializableStateXml(EqlModuleProcessor& processor)
{
    return createSerializableStateXml(processor.getValueTreeState(),
                                      processor.getActiveFilterCount());
}

std::unique_ptr<juce::XmlElement> createSerializableStateXml(juce::AudioProcessorValueTreeState& parameters,
                                                             const int activeFilterCount)
{
    auto state = parameters.copyState();
    state.setProperty(EqlModuleProcessor::activeFilterCountStateKey, activeFilterCount, nullptr);

    auto stateXml = state.createXml();

    if (stateXml == nullptr)
        return {};

    return stateXml;
}
