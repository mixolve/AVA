#include "PresetStorage.h"

#if JUCE_MAC || JUCE_LINUX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
juce::CriticalSection& catalogThreadLock() { static juce::CriticalSection lock; return lock; }
juce::InterProcessLock& catalogProcessLock() { static juce::InterProcessLock lock("mixolve.ava.eql.preset-catalog"); return lock; }
unsigned catalogLockDepth = 0; // guarded by catalogThreadLock
}
PresetCatalogTransaction::PresetCatalogTransaction() : threadLock(catalogThreadLock())
{
    if (catalogLockDepth != 0 || catalogProcessLock().enter(3000)) { ++catalogLockDepth; acquired = true; }
}
PresetCatalogTransaction::~PresetCatalogTransaction()
{
    if (acquired && --catalogLockDepth == 0) catalogProcessLock().exit();
}

static bool writeXmlToFile(const juce::XmlElement& element, const juce::File& file);

static juce::File getFilterPresetsDirectory()
{
    auto directory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile(presetStorageVendorFolder)
                         .getChildFile(presetStorageProductFolder);

    directory = directory.getChildFile(eqlPresetStorageModuleFolder)
                         .getChildFile(presetStorageRootFolder);
    directory.createDirectory();

    return directory;
}

static juce::String makePresetFileStem(const juce::String& presetName)
{
    return juce::File::createLegalFileName(presetName.trim()).trim();
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
        if (outputStream->getStatus().failed())
            return false;
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

// The journal keeps the old collection until a durable commit marker exists.
// Readers recover it while holding the same interprocess catalog transaction.
static juce::File getCatalogJournal(const juce::File& directory)
{
    return directory.getChildFile(".catalog-transaction");
}

static bool syncCatalogDirectory(const juce::File& directory)
{
#if JUCE_MAC || JUCE_LINUX
    const auto descriptor = ::open(directory.getFullPathName().toRawUTF8(), O_RDONLY);
    if (descriptor < 0) return false;
    const auto result = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return result;
#else
    juce::ignoreUnused(directory);
    return true;
#endif
}

static bool restoreCatalogFile(const juce::File& file, const juce::MemoryBlock& bytes)
{
    juce::TemporaryFile temporary(file);
    auto stream = temporary.getFile().createOutputStream();
    if (stream == nullptr) return false;
    stream->setPosition(0);
    stream->truncate();
    if (!stream->write(bytes.getData(), bytes.getSize())) return false;
    stream->flush();
    if (stream->getStatus().failed()) return false;
    stream.reset();
    return temporary.overwriteTargetFileWithTemporary();
}

static bool recoverPresetCatalog(const juce::File& directory)
{
    const auto journal = getCatalogJournal(directory);
    if (!journal.exists()) return true;
    const auto state = loadXmlFile(journal);
    if (state == nullptr || !state->hasTagName("CATALOG_TRANSACTION")) return false;
    const auto phase = state->getStringAttribute("phase");
    if (phase != "prepared" && phase != "committed") return false;
    if (phase == "prepared") {
        struct Entry { juce::File file; bool existed; juce::MemoryBlock bytes; };
        std::vector<Entry> entries;
        juce::StringArray names;
        for (const auto* item : state->getChildIterator()) {
            const auto name = item->getStringAttribute("file");
            if (!item->hasTagName("FILE") || name.isEmpty() || name != juce::File::createLegalFileName(name)
                || !name.endsWithIgnoreCase(".xml") || containsFileNameIgnoreCase(names, name)) return false;
            Entry entry {directory.getChildFile(name), item->getBoolAttribute("existed"), {}};
            if (entry.existed && !entry.bytes.fromBase64Encoding(item->getAllSubText())) return false;
            names.add(name);
            entries.push_back(std::move(entry));
        }
        for (const auto& entry : entries) {
            if (entry.existed) {
                if (!restoreCatalogFile(entry.file, entry.bytes)) return false;
            } else if (entry.file.exists() && !entry.file.deleteFile()) return false;
        }
        if (!syncCatalogDirectory(directory)) return false;
    }
    return journal.deleteFile() && syncCatalogDirectory(directory);
}

static bool writePresetCollectionToDirectory(const juce::File& directory,
                                             const juce::XmlElement& rootElement,
                                             const juce::String& presetElementTag)
{
    if (!recoverPresetCatalog(directory)) return false;
    juce::StringArray expectedFileNames;

    // Check all filenames before changing files: legal-name conversion can
    // map two different preset names to the same file.
    for (auto* child : rootElement.getChildIterator())
    {
        if (! child->hasTagName(presetElementTag))
            continue;

        const auto presetName = child->getStringAttribute("name").trim();
        if (makePresetFileStem(presetName).isEmpty())
            return false;

        const auto fileName = getPresetFileForName(directory, presetName).getFileName();
        if (containsFileNameIgnoreCase(expectedFileNames, fileName))
            return false;

        expectedFileNames.add(fileName);
    }

    if (directory.createDirectory().failed())
        return false;

    struct Backup { juce::File file; juce::MemoryBlock bytes; };
    std::vector<Backup> backups;
    juce::Array<juce::File> originalFiles;
    directory.findChildFiles(originalFiles, juce::File::findFiles, false, "*.xml");
    for (const auto& file : originalFiles) {
        const auto xml = loadXmlFile(file);
        if (xml == nullptr || !xml->hasTagName(presetElementTag)) continue;
        const auto name = xml->getStringAttribute("name").trim();
        if (name.isEmpty() || file.getFileName() != getPresetFileForName(directory, name).getFileName()) continue;
        Backup backup {file, {}};
        if (!file.loadFileAsData(backup.bytes)) return false;
        backups.push_back(std::move(backup));
    }
    juce::XmlElement journal("CATALOG_TRANSACTION");
    journal.setAttribute("phase", "prepared");
    for (const auto& backup : backups) {
        auto* entry = journal.createNewChildElement("FILE");
        entry->setAttribute("file", backup.file.getFileName());
        entry->setAttribute("existed", true);
        entry->addTextElement(backup.bytes.toBase64Encoding());
    }
    for (const auto& name : expectedFileNames) {
        if (std::any_of(backups.begin(), backups.end(), [&name](const auto& backup) {
            return backup.file.getFileName().equalsIgnoreCase(name);
        })) continue;
        // An unrelated file occupying a requested name must not be overwritten.
        if (directory.getChildFile(name).exists()) return false;
        auto* entry = journal.createNewChildElement("FILE");
        entry->setAttribute("file", name);
        entry->setAttribute("existed", false);
    }
    const auto journalFile = getCatalogJournal(directory);
    if (!writeXmlToFile(journal, journalFile) || !syncCatalogDirectory(directory)) return false;
    bool committed = false;
    const juce::ScopeGuard rollback {[&] {
        if (!committed) recoverPresetCatalog(directory);
    }};
    for (auto* child : rootElement.getChildIterator())
    {
        if (! child->hasTagName(presetElementTag))
            continue;

        const auto presetName = child->getStringAttribute("name").trim();

        const auto presetFile = getPresetFileForName(directory, presetName);

        auto presetCopy = std::make_unique<juce::XmlElement>(*child);
        presetCopy->removeAttribute(EqlModuleProcessor::filterPresetSelectedStateKey);

        if (auto* stateElement = presetCopy->getChildByName("eql_state"))
        {
            stateElement->removeAttribute(EqlModuleProcessor::filterPresetSelectedStateKey);
        }

        if (const auto existing = loadXmlFile(presetFile); existing != nullptr && presetCopy->isEquivalentTo(existing.get(), true)) continue;
        if (! writeXmlToFile(*presetCopy, presetFile)) return false;
    }

    for (const auto& backup : backups)
        if (!containsFileNameIgnoreCase(expectedFileNames, backup.file.getFileName())) {
            juce::MemoryBlock current;
            if (!backup.file.loadFileAsData(current) || current != backup.bytes || !backup.file.deleteFile()) return false;
        }
    if (!syncCatalogDirectory(directory)) return false;
    journal.setAttribute("phase", "committed");
    if (!writeXmlToFile(journal, journalFile) || !syncCatalogDirectory(directory)) return false;
    committed = true;
    // A committed journal can safely be cleaned by the next reader if deletion fails.
    if (journalFile.deleteFile()) syncCatalogDirectory(directory);
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

juce::String getFilterPresetCatalogStamp()
{
    const PresetCatalogTransaction transaction;
    if (!transaction || !recoverPresetCatalog(getFilterPresetsDirectory())) return {};
    juce::Array<juce::File> files;
    getFilterPresetsDirectory().findChildFiles(files, juce::File::findFiles, false, "*.xml");
    juce::StringArray entries;
    for (const auto& file : files)
        entries.add(file.getFileName() + ":" + juce::String(file.getSize()) + ":"
            + juce::String(file.getLastModificationTime().toMilliseconds()));
    entries.sort(false);
    return entries.joinIntoString("\n");
}

std::unique_ptr<juce::XmlElement> loadFilterPresetsXml()
{
    const PresetCatalogTransaction transaction;
    if (!transaction || !recoverPresetCatalog(getFilterPresetsDirectory())) return {};
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
    const PresetCatalogTransaction transaction;
    if (!transaction) return false;
    return writePresetCollectionToDirectory(getFilterPresetsDirectory(), rootElement, presetTag);
}

juce::ValueTree copySerializableState(EqlModuleProcessor& processor)
{
    auto state = processor.getValueTreeState().copyState();
    state.setProperty(EqlModuleProcessor::activeFilterCountStateKey, processor.getActiveFilterCount(), nullptr);
    return state;
}
