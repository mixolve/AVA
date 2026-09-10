#include "ProcessorSupport.h"

#include <algorithm>
#include <cmath>
#include <vector>

static bool writeXmlToFile(const juce::XmlElement& element, const juce::File& file);

static juce::File getDocumentsPresetStorageDirectory()
{
    auto directory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

#if ! JUCE_IOS
    directory = directory.getChildFile(presetStorageVendorFolder)
                         .getChildFile(presetStorageProductFolder);
#endif

    directory = directory.getChildFile(eqlPresetStorageModuleFolder)
                         .getChildFile(presetStorageRootFolder);
    directory.createDirectory();

    return directory;
}

#if JUCE_IOS
static juce::File getAppGroupPresetStorageDirectory()
{
    auto directory = getEqlAppGroupContainerDirectory();

    if (! directory.isDirectory())
        return {};

    directory = directory.getChildFile(eqlPresetStorageModuleFolder)
                         .getChildFile(presetStorageRootFolder);
    directory.createDirectory();

    return directory;
}

#if ! AVA_APP_EXTENSION
static bool shouldCopyPresetFile(const juce::File& sourceFile, const juce::File& targetFile)
{
    return ! targetFile.existsAsFile() || ! sourceFile.hasIdenticalContentTo(targetFile);
}

static void copyPresetFilesExactly(const juce::File& sourceDirectory, const juce::File& targetDirectory)
{
    targetDirectory.createDirectory();

    if (! sourceDirectory.isDirectory() || ! targetDirectory.isDirectory())
        return;

    juce::Array<juce::File> sourceFiles;
    sourceDirectory.findChildFiles(sourceFiles, juce::File::findFiles, false, "*.xml");

    juce::StringArray expectedFileNames;

    for (const auto& sourceFile : sourceFiles)
    {
        expectedFileNames.add(sourceFile.getFileName());

        const auto targetFile = targetDirectory.getChildFile(sourceFile.getFileName());

        if (shouldCopyPresetFile(sourceFile, targetFile))
        {
            if (targetFile.existsAsFile())
                targetFile.deleteFile();

            sourceFile.copyFileTo(targetFile);
        }
    }

    juce::Array<juce::File> existingFiles;
    targetDirectory.findChildFiles(existingFiles, juce::File::findFiles, false, "*.xml");

    for (const auto& existingFile : existingFiles)
    {
        if (! expectedFileNames.contains(existingFile.getFileName(), true))
            existingFile.deleteFile();
    }
}

static void mirrorDocumentsPresetsToAppGroup(const juce::File& documentsDirectory)
{
    const auto appGroupDirectory = getAppGroupPresetStorageDirectory();

    if (! appGroupDirectory.isDirectory())
        return;

    if (documentsDirectory.getFullPathName().equalsIgnoreCase(appGroupDirectory.getFullPathName()))
        return;

    copyPresetFilesExactly(documentsDirectory, appGroupDirectory);
}
#endif

#endif

void syncEqlPresetStorageWithSharedContainer()
{
#if JUCE_IOS && ! AVA_APP_EXTENSION
    mirrorDocumentsPresetsToAppGroup(getDocumentsPresetStorageDirectory());
#endif
}

static juce::File getPresetStorageRootDirectory()
{
#if JUCE_IOS && AVA_APP_EXTENSION
    auto appGroupDirectory = getAppGroupPresetStorageDirectory();

    if (appGroupDirectory.isDirectory())
        return appGroupDirectory;
#endif

    return getDocumentsPresetStorageDirectory();
}

static juce::File getFilterPresetsDirectory()
{
    auto directory = getPresetStorageRootDirectory();

    directory.createDirectory();

#if JUCE_IOS && ! AVA_APP_EXTENSION
    mirrorDocumentsPresetsToAppGroup(directory);
#endif

    return directory;
}

static juce::String makePresetFileStem(const juce::String& presetName)
{
    const auto trimmedName = presetName.trim();
    auto fileStem = juce::File::createLegalFileName(trimmedName.toLowerCase()).trim();

    if (fileStem.isEmpty())
        fileStem = "preset";

    return fileStem;
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

static int getStoredFilterParameterIndex(const juce::String& parameterId)
{
    const auto trimmedId = parameterId.trim();

    const auto prefix = trimmedId.startsWithIgnoreCase("filter_") ? juce::String("filter_")
                                                                  : juce::String();

    if (prefix.isEmpty())
        return -1;

    const auto remainder = trimmedId.substring(prefix.length());
    const auto separatorIndex = remainder.indexOfChar('_');

    if (separatorIndex <= 0)
        return -1;

    const auto numberText = remainder.substring(0, separatorIndex);

    if (! numberText.containsOnly("0123456789"))
        return -1;

    const auto normalizedSuffix = remainder.substring(separatorIndex + 1).trim().toLowerCase();

    if (normalizedSuffix.isEmpty())
        return -1;

    if (normalizedSuffix != "type"
        && normalizedSuffix != "place"
        && normalizedSuffix != "slope"
        && normalizedSuffix != "frequency"
        && normalizedSuffix != "bandwidth"
        && normalizedSuffix != "gain"
        && normalizedSuffix != "bypass")
    {
        return -1;
    }

    return numberText.getIntValue();
}

static int getStoredFilterParameterSortRank(const juce::String& parameterId)
{
    const auto trimmedId = parameterId.trim();

    const auto prefix = trimmedId.startsWithIgnoreCase("filter_") ? juce::String("filter_")
                                                                    : juce::String();

    if (prefix.isEmpty())
        return 100;

    const auto remainder = trimmedId.substring(prefix.length());
    const auto separatorIndex = remainder.indexOfChar('_');

    if (separatorIndex <= 0)
        return 100;

    const auto numberText = remainder.substring(0, separatorIndex);

    if (! numberText.containsOnly("0123456789"))
        return 100;

    const auto suffix = remainder.substring(separatorIndex + 1).trim().toLowerCase();

    if (suffix.isEmpty())
        return 100;

    const auto filterIndex = numberText.getIntValue() - 1;
    const auto parameterRank = suffix == "type" ? 0
        : suffix == "place" ? 1
        : suffix == "slope" ? 2
        : suffix == "frequency" ? 3
        : suffix == "bandwidth" ? 4
        : suffix == "gain" ? 5
        : suffix == "bypass" ? 6
        : 8;

    return (filterIndex * 10) + parameterRank + 1;
}

static juce::String getStoredFilterParameterSuffix(const juce::String& parameterId)
{
    const auto trimmedId = parameterId.trim();

    if (! trimmedId.startsWithIgnoreCase("filter_"))
        return {};

    const auto remainder = trimmedId.substring(7);
    const auto separatorIndex = remainder.indexOfChar('_');

    if (separatorIndex <= 0)
        return {};

    return remainder.substring(separatorIndex + 1).trim().toLowerCase();
}

static float readPlainParameterValue(juce::AudioProcessorValueTreeState& parameters,
                                     const juce::String& parameterId)
{
    if (auto* parameter = parameters.getParameter(parameterId))
        return parameter->convertFrom0to1(parameter->getValue());

    return 0.0f;
}

static bool shouldStoreFilterParameter(const juce::String& parameterId,
                                       juce::AudioProcessorValueTreeState& parameters,
                                       const int storageFilterIndex)
{
    const auto suffix = getStoredFilterParameterSuffix(parameterId);

    if (suffix.isEmpty())
        return true;

    const auto filterIndex = storageFilterIndex - 1;

    if (filterIndex < 0 || filterIndex >= EqlModuleProcessor::maxFilterCount)
        return false;

    const auto typeChoice = static_cast<int>(std::lround(readPlainParameterValue(parameters,
                                                                                 EqlModuleProcessor::getFilterTypeParamId(filterIndex))));
    const auto filterType = EqlModuleProcessor::filterTypeFromChoiceIndex(typeChoice);

    if (filterType == EqlModuleProcessor::FilterType::volume)
    {
        return suffix == "type"
            || suffix == "place"
            || suffix == "gain"
            || suffix == "bypass";
    }

    return true;
}

static bool shouldFormatFilterParameterValueAsTwoDecimals(const juce::String& parameterId)
{
    const auto trimmedId = parameterId.trim().toLowerCase();
    return trimmedId.endsWith("_frequency")
        || trimmedId.endsWith("_gain")
        || trimmedId.endsWith("_bandwidth");
}

static bool shouldFormatFilterParameterValueAsTwoDigits(const juce::String& parameterId)
{
    const auto trimmedId = parameterId.trim().toLowerCase();
    return trimmedId.endsWith("_type")
        || trimmedId.endsWith("_place")
        || trimmedId.endsWith("_slope")
        || trimmedId.endsWith("_bypass");
}

static void formatStoredParameterValue(juce::XmlElement& element)
{
    const auto parameterId = element.getStringAttribute("id").trim();

    if (! shouldFormatFilterParameterValueAsTwoDecimals(parameterId))
    {
        if (! shouldFormatFilterParameterValueAsTwoDigits(parameterId))
            return;

        const auto value = static_cast<int>(std::lround(element.getDoubleAttribute("value", 0.0)));
        element.setAttribute("value", juce::String::formatted("%02d", value));
        return;
    }

    const auto value = element.getDoubleAttribute("value", 0.0);
    element.setAttribute("value", juce::String::formatted("%.2f", static_cast<double>(value)));
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

    for (auto* child : rootElement.getChildIterator())
    {
        if (! child->hasTagName(presetElementTag))
            continue;

        const auto presetName = child->getStringAttribute("name").trim();

        if (presetName.isEmpty())
            continue;

        auto presetCopy = std::make_unique<juce::XmlElement>(*child);
        presetCopy->removeAttribute(EqlModuleProcessor::filterPresetLastSelectedStateKey);
        presetCopy->removeAttribute(EqlModuleProcessor::filterPresetDefaultSelectedStateKey);

        if (auto* stateElement = presetCopy->getChildByName("eql_state"))
        {
            stateElement->removeAttribute(EqlModuleProcessor::filterPresetLastSelectedStateKey);
            stateElement->removeAttribute(EqlModuleProcessor::filterPresetDefaultSelectedStateKey);
        }

        if (! writeXmlToFile(*presetCopy, getPresetFileForName(directory, presetName)))
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

        if (presetName.isEmpty())
            continue;

        if (auto* existingPreset = findPresetElement(*collection, presetName))
            collection->removeChildElement(existingPreset, true);

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

#if JUCE_IOS && ! AVA_APP_EXTENSION
    mirrorDocumentsPresetsToAppGroup(filterPresetsDirectory);
#endif

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

    struct ChildElementWithSortKey
    {
        int sortRank = 0;
        std::unique_ptr<juce::XmlElement> element;
    };

    std::vector<ChildElementWithSortKey> childElements;

    for (auto* child : stateXml->getChildIterator())
    {
        const auto liveParameterId = child->hasTagName("PARAM")
            ? child->getStringAttribute("id").trim()
            : juce::String();

        if (liveParameterId.isEmpty() || parameters.getParameter(liveParameterId) == nullptr)
            continue;

        auto childCopy = std::make_unique<juce::XmlElement>(*child);

        formatStoredParameterValue(*childCopy);

        const auto parameterId = childCopy->getStringAttribute("id").trim();
        const auto filterIndex = getStoredFilterParameterIndex(parameterId);

        if (filterIndex < 0 && parameterId.startsWithIgnoreCase("filter_"))
            continue;

        if (filterIndex > activeFilterCount)
            continue;

        if (filterIndex > 0 && ! shouldStoreFilterParameter(parameterId, parameters, filterIndex))
            continue;

        childElements.push_back({ getStoredFilterParameterSortRank(parameterId), std::move(childCopy) });
    }

    std::sort(childElements.begin(),
              childElements.end(),
              [] (const ChildElementWithSortKey& left, const ChildElementWithSortKey& right)
              {
                  return left.sortRank < right.sortRank;
              });

    auto storageState = std::make_unique<juce::XmlElement>(stateXml->getTagName());
    storageState->setAttribute(EqlModuleProcessor::activeFilterCountStateKey, activeFilterCount);

    const auto lastSelectedPreset = state.getProperty(EqlModuleProcessor::filterPresetLastSelectedStateKey).toString().trim();
    if (lastSelectedPreset.isNotEmpty())
        storageState->setAttribute(EqlModuleProcessor::filterPresetLastSelectedStateKey, lastSelectedPreset);

    const auto defaultSelectedPreset = state.getProperty(EqlModuleProcessor::filterPresetDefaultSelectedStateKey).toString().trim();
    if (defaultSelectedPreset.isNotEmpty())
        storageState->setAttribute(EqlModuleProcessor::filterPresetDefaultSelectedStateKey, defaultSelectedPreset);

    for (auto& childElement : childElements)
        storageState->addChildElement(childElement.element.release());

    return storageState;
}
