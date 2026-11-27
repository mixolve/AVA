#include "Processor.h"
#include "Editor.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "ModuleCopyTargets.h"
#include "ModuleSlotTargets.h"
#include "ModuleStackState.h"
#include "../routing/State.h"

#include <algorithm>
#include "../crossover/ParameterAccess.h"

namespace {
using Slot=ava::module_stack::Slot;

void moveSlotToShownIndex(ava::module_stack::Order& slots, int uid, size_t shownIndex)
{
    const auto source = std::find_if(slots.begin(), slots.end(), [uid](const Slot& slot) { return slot.uid == uid; });
    if (source == slots.end()) return;
    const auto slot = *source;
    slots.erase(source);
    auto destination = slots.end();
    for (auto it = slots.begin(); it != slots.end(); ++it)
        if (it->shown && shownIndex-- == 0)
        {
            destination = it;
            break;
        }
    slots.insert(destination, slot);
}

juce::var asObject() { return juce::var(new juce::DynamicObject()); }
void put(juce::var& object, const juce::Identifier& name, const juce::var& value) {
    if(auto* ptr=object.getDynamicObject())ptr->setProperty(name,value);
}
}

std::vector<Slot> AvaAudioProcessor::getModuleStackForRange(const size_t band) const
{
    const juce::ScopedLock lock(processingLock);
    return band<moduleStacks.size() ? moduleStacks[band] : ava::module_stack::Order{};
}

void AvaAudioProcessor::resetModuleCopies()
{
    moduleCopies.clear();
    for(size_t band=0;band<moduleStacks.size();++band) {
        moduleStacks[band]=ava::module_stack::initial(getModuleOrderForRange(band));
        lastCreatedModuleNumbers[band].fill(1);
    }
    nextModuleCopyUid=ava::module_stack::originalCount;
    selectedModuleCopy.store(-1);
}

std::shared_ptr<AvaAudioProcessor::ModuleCopy> AvaAudioProcessor::makeModuleCopy(
    int type,int band,int uid,int serial)
{
    if(type<0||type>=ava::module_stack::typeCount||band<0||band>=int(moduleStacks.size()))return {};
    auto result=std::make_shared<ModuleCopy>();
    result->type=type;result->range=band;result->uid=uid;result->serial=serial;
    const auto requiredCount=juce::jmax(size_t(band+1),size_t(getCrossoverSettings().activeSplitCount+1));
    if(module_order::isChain(type)) {
        result->blocks=std::make_shared<BlockChainProcessor>(*this);
        result->blocks->ensureRangeCount(requiredCount);
        // A newly added block must be audible even if its original is hidden.
        if(auto* parameter=result->blocks->getValueTreeState().getParameter(
            ava::crossover::parameters::makeRangeParameterId(size_t(band),
                ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(type))].hiddenSuffix)))
            parameter->setValueNotifyingHost(0.0f);
        result->blocks->markParametersDirty();
        result->blocks->syncParameters(true);
    } else if(type==module_order::dyn) {
        result->dyn=std::make_shared<DynModuleProcessor>(*this);
        result->dyn->ensureRangeCount(requiredCount);
        result->dyn->syncParameters(true);
    } else {
        result->eql=std::make_shared<EqlProcessorBank>();
        result->eql->ensureRangeCount(requiredCount);
    }
    if(currentSampleRate>0.0&&preparedBlockSize>0) {
        if(result->blocks) result->blocks->prepareToPlay(currentSampleRate,preparedBlockSize);
        if(result->dyn) result->dyn->prepareToPlay(currentSampleRate,preparedBlockSize);
        if(result->eql) result->eql->prepareToPlay(currentSampleRate,preparedBlockSize);
    }
    return result;
}

bool AvaAudioProcessor::isModuleSlotBypassed(const size_t band, const int uid) const
{
    const auto slots = getModuleStackForRange(band);
    const auto* slot = ava::module_stack::find(slots, uid);
    if (slot == nullptr || !slot->enabled) return true;
    if (uid < ava::module_stack::originalCount)
    {
        if (module_order::isChain(slot->type) && blockChainModuleProcessor != nullptr)
        {
            const auto hiddenId = ava::crossover::parameters::makeRangeParameterId(band,
                ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(slot->type))].hiddenSuffix);
            if (const auto* hidden = blockChainModuleProcessor->getValueTreeState().getParameter(hiddenId))
                return hidden->getValue() >= 0.5f;
        }
        else if (slot->type == module_order::eql || slot->type == module_order::dyn)
            return isModuleHiddenForRange(band, slot->type);
    }
    return false;
}

int AvaAudioProcessor::insertModuleAt(size_t band, int type, size_t shownIndex)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    if (band >= moduleStacks.size()) return -1;
    auto& stack = moduleStacks[band];
    const auto inactive = std::find_if(stack.begin(), stack.end(), [type](const Slot& slot)
    {
        return slot.type == type && !slot.shown;
    });
    if (inactive != stack.end())
    {
        const auto uid = inactive->uid;
        moveSlotToShownIndex(stack, uid, shownIndex);
        return setModuleSlotShown(band, uid, true) ? uid : -1;
    }
    return addModuleCopy(band, type, shownIndex);
}

int AvaAudioProcessor::addModuleCopy(size_t band, int sourceType, std::optional<size_t> shownIndex)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    if (band >= moduleStacks.size() || !juce::isPositiveAndBelow(sourceType, ava::module_stack::typeCount)
        || moduleStacks[band].size() >= ava::module_stack::maximumSlotsPerBand)
        return -1;
    if (nextModuleCopyUid >= ava::module_stack::state::maximumSavedSerial
        || lastCreatedModuleNumbers[band][size_t(sourceType)] >= ava::module_stack::state::maximumSavedSerial)
        return -1;
    const auto id=nextModuleCopyUid;
    // Also account for the serials already present in the stack, so a copy
    // can never be assigned the original module's serial (1).
    const auto serial=ava::module_stack::nextSerial(
        moduleStacks[band], sourceType, lastCreatedModuleNumbers[band][size_t(sourceType)]);
    auto copy=makeModuleCopy(sourceType,int(band),id,serial);
    if(copy==nullptr||!ava::module_stack::insertCopy(moduleStacks[band],sourceType,id,serial))return -1;
    if (shownIndex) moveSlotToShownIndex(moduleStacks[band], id, *shownIndex);
    nextModuleCopyUid++;
    lastCreatedModuleNumbers[band][size_t(sourceType)]=serial;
    moduleCopies.emplace(id,std::move(copy));
    registerModuleSlotBypassTarget(band, id, sourceType);
    registerLoadedModuleStateListeners();
    publishRenderGraph();
    updateShellLatency();
    assignNewModuleBypassToHostSlot(band, id);
    notifyHostOfStateChange();
    return id;
}

bool AvaAudioProcessor::deleteModuleSlot(size_t band,int uid)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    if (band >= moduleStacks.size()) return false;
    const auto* target = ava::module_stack::find(moduleStacks[band], uid);
    if (target == nullptr) return false;
    const auto type = target->type;
    if (!ava::module_stack::remove(moduleStacks[band], uid)) return false;
    // Unbind the owner's automation source before a slot's BP proxy can die.
    invalidateHostSlotTargets();
    if(uid>=ava::module_stack::originalCount) {
        // Slots and module-state observers may point to the copied APVTS.
        // Both listener sets must be detached before destroying the copy.
        clearLoadedModuleStateListeners();
        if(selectedModuleCopy.load()==uid)selectedModuleCopy.store(-1);
        moduleCopies.erase(uid);
    } else if (module_order::isChain(type) && blockChainModuleProcessor) {
        const auto& description=ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(type))];
        auto& state=blockChainModuleProcessor->getValueTreeState();
        const auto paramId=ava::crossover::parameters::makeRangeParameterId(band,description.hiddenSuffix);
        if(auto* parameter=state.getParameter(paramId))parameter->setValueNotifyingHost(1.0f);
    } else if(uid==module_order::eql || uid==module_order::dyn) {
        setModuleHiddenForRange(band,uid,true);
    }
    // A deleted tile also loses its host-slot BP binding.  Do this for both
    // originals and copies, without touching any other module or instance.
    auto& owner = getRoutingOwner();
    for (int index = 0; index < assignableHostSlotCount; ++index)
    {
        const auto assigned = owner.getHostSlotTargetId(index);
        int instanceId = 0;
        juce::String local;
        const auto qualified = ava::routing::parseParameterTarget(assigned, instanceId, local);
        if (qualified && instanceId != getRoutingInstanceId()) continue;
        if (!qualified) local = assigned;
        int copyUid = -1;
        juce::String copyParameter;
        size_t moduleBand = 0;
        int moduleUid = -1;
        const bool isDeletedCopyParameter = uid >= ava::module_stack::originalCount
            && ava::module_copy_targets::parse(local, copyUid, copyParameter) && copyUid == uid;
        const bool isDeletedModuleBypass = ava::module_slot_targets::parse(local, moduleBand, moduleUid)
            && moduleBand == band && moduleUid == uid;
        if (isDeletedCopyParameter || isDeletedModuleBypass)
            owner.clearHostSlotAssignment(index);
    }
    moduleSlotBypassTargets.erase(std::make_pair(band, uid));
    registerLoadedModuleStateListeners();
    publishRenderGraph();
    refreshHostSlotTargets();
    updateShellLatency();
    notifyHostOfStateChange();
    return true;
}

// The MODULES tile activates/deactivates the module AND shows/hides its editor.
// BP remains independent: hiding must not modify slot.enabled or its host target.
// Re-publish the graph and update PDC so switched-off modules do not process audio.
bool AvaAudioProcessor::setModuleSlotShown(size_t band, int uid, bool shown)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    if (band >= moduleStacks.size()) return false;
    auto* slot = ava::module_stack::find(moduleStacks[band], uid);
    if (slot == nullptr || slot->shown == shown) return false;
    slot->shown = shown;
    publishRenderGraph();
    updateShellLatency();
    notifyHostOfStateChange();
    return true;
}

bool AvaAudioProcessor::setModuleSlotEnabled(size_t band,int uid,bool enabled)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    if(band>=moduleStacks.size())return false;
    auto* slot=ava::module_stack::find(moduleStacks[band],uid);
    if(slot==nullptr)return false;
    // Original blocks also have a hidden flag. Keep it in sync with
    // the per-slot bypass so a single click always has an audible result.
    bool changed=slot->enabled!=enabled;
    if (uid < ava::module_stack::originalCount)
    {
        if (module_order::isChain(slot->type) && blockChainModuleProcessor != nullptr)
        {
            const auto parameterId=ava::crossover::parameters::makeRangeParameterId(band,
                ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(slot->type))].hiddenSuffix);
            if(auto* hidden=blockChainModuleProcessor->getValueTreeState().getParameter(parameterId))
                if ((hidden->getValue() >= 0.5f) == enabled)
                {
                    changed=true;
                    hidden->setValueNotifyingHost(enabled ? 0.0f : 1.0f);
                }
        }
        else if (slot->type == module_order::eql || slot->type == module_order::dyn)
        {
            if (isModuleHiddenForRange(band,slot->type) == enabled)
            {
                changed=true;
                setModuleHiddenForRange(band,slot->type,!enabled);
            }
        }
    }
    if(!changed)return false;
    slot->enabled=enabled;
    publishRenderGraph();
    updateShellLatency();
    if (auto target = moduleSlotBypassTargets.find(std::make_pair(band, uid));
        target != moduleSlotBypassTargets.end())
        target->second->sendValueChangedMessageToListeners(target->second->getValue());
    notifyHostOfStateChange();
    return true;
}

bool AvaAudioProcessor::moveModuleSlots(size_t band,const std::vector<int>& order)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    if(band>=moduleStacks.size()||!ava::module_stack::reorder(moduleStacks[band],order))return false;
    publishRenderGraph();
    updateShellLatency();
    notifyHostOfStateChange();
    return true;
}

bool AvaAudioProcessor::selectModuleCopy(int uid)
{
    if(uid>=ava::module_stack::originalCount&&!moduleCopies.contains(uid))return false;
    selectedModuleCopy.store(uid>=ava::module_stack::originalCount?uid:-1);
    return true;
}
BlockChainProcessor* AvaAudioProcessor::getCopiedBlockChainProcessor(int uid) noexcept
{
    const auto it=moduleCopies.find(uid);
    return it!=moduleCopies.end()?it->second->blocks.get():nullptr;
}
DynModuleProcessor* AvaAudioProcessor::getCopiedDynProcessor(int uid) noexcept
{
    const auto it=moduleCopies.find(uid);
    return it!=moduleCopies.end()?it->second->dyn.get():nullptr;
}
EqlModuleProcessor* AvaAudioProcessor::getCopiedEqlProcessor(int uid, size_t band) noexcept
{
    const auto it = moduleCopies.find(uid);
    return it != moduleCopies.end() && it->second->eql
        ? it->second->eql->getProcessor(band) : nullptr;
}

BlockChainProcessor* AvaAudioProcessor::getEditingBlockChainProcessor() noexcept
{
    const auto it=moduleCopies.find(selectedModuleCopy.load());
    return it!=moduleCopies.end() && it->second->blocks ? it->second->blocks.get() : blockChainModuleProcessor.get();
}
DynModuleProcessor* AvaAudioProcessor::getEditingDynModuleProcessor() noexcept
{
    const auto it=moduleCopies.find(selectedModuleCopy.load());
    return it!=moduleCopies.end() && it->second->dyn ? it->second->dyn.get() : dynModuleProcessor.get();
}
EqlProcessorBank* AvaAudioProcessor::getEditingEqlProcessorBank() noexcept
{
    const auto it=moduleCopies.find(selectedModuleCopy.load());
    return it!=moduleCopies.end() && it->second->eql ? it->second->eql.get() : eqlProcessorBank.get();
}
EqlModuleProcessor* AvaAudioProcessor::getEditingEqlProcessor() noexcept
{
    auto* bank=getEditingEqlProcessorBank();
    if(bank==nullptr)return nullptr;
    return bank->getProcessor(getSelectedCrossoverRange());
}

void AvaAudioProcessor::saveModuleCopies(juce::ValueTree& state) const
{
    juce::var root=asObject();
    put(root,"nextId",nextModuleCopyUid);
    juce::Array<juce::var> bands;
    for(size_t band=0;band<moduleStacks.size();++band) {
        juce::var range=asObject();
        juce::Array<juce::var> slots;
        for(const auto& slot:moduleStacks[band]) {
            juce::var value=asObject();
            put(value,"id",slot.uid);put(value,"type",slot.type);
            put(value,"created",slot.created);put(value,"enabled",slot.enabled);put(value,"shown",slot.shown);
            slots.add(value);
        }
        juce::Array<juce::var> serials;
        for (int type = 0; type < ava::module_stack::originalCount; ++type)
            serials.add(lastCreatedModuleNumbers[band][size_t(type)]);
        juce::var addedCreated = asObject();
        for (int type = ava::module_stack::originalCount; type < ava::module_stack::typeCount; ++type)
            put(addedCreated, juce::Identifier(juce::String(type)),
                lastCreatedModuleNumbers[band][size_t(type)]);
        put(range, "addedCreated", addedCreated);
        put(range, "slots", juce::var(slots));
        put(range, "lastCreated", juce::var(serials));
        bands.add(range);
    }
    put(root,"bands",juce::var(bands));
    juce::Array<juce::var> copies;
    for(const auto& [id,copy]:moduleCopies) {
        juce::MemoryBlock bytes;
        if(copy->blocks)copy->blocks->getStateInformation(bytes);
        else if(copy->dyn)copy->dyn->getStateInformation(bytes);
        else if(copy->eql)copy->eql->getStateInformation(bytes);
        juce::var value=asObject();
        put(value,"id",id);
        put(value,"state",bytes.toBase64Encoding());
        copies.add(value);
    }
    put(root,"copies",juce::var(copies));
    state.setProperty(moduleStackStateKey,juce::JSON::toString(root,true),nullptr);
}

// Apply saved stack changes without discarding unchanged module processors.
// The same implementation handles project load, host Undo and pasted states:
// new UIDs are created, removed UIDs are released, existing UIDs retain their
// processors and editor attachments.
bool AvaAudioProcessor::restoreModuleCopies(const juce::ValueTree& state)
{
    if (!state.hasProperty(moduleStackStateKey))
    {
        if (!moduleCopies.empty())
        {
            invalidateHostSlotTargets();
            clearLoadedModuleStateListeners();
            moduleCopies.clear();
        }
        for (size_t band = 0; band < moduleStacks.size(); ++band)
        {
            moduleStacks[band] = ava::module_stack::initial(getModuleOrderForRange(band));
            lastCreatedModuleNumbers[band].fill(1);
        }
        nextModuleCopyUid = ava::module_stack::originalCount;
        selectedModuleCopy.store(-1);
        return true;
    }

    const auto decoded = ava::module_stack::state::decode(
        state.getProperty(moduleStackStateKey).toString(), moduleStacks.size());
    if (!decoded.has_value()) return false;

    const auto& nextStacks = decoded->stacks;
    const auto& nextCreatedNumbers = decoded->lastCreated;
    const auto nextId = decoded->nextUid;

    // Stage additions before touching live stacks. Reuse the old processor
    // whenever its UID, band and module type are unchanged.
    std::map<int, std::shared_ptr<ModuleCopy>> nextCopies;
    // Verify every state blob before touching a reused processor. Otherwise
    // a malformed later entry can leave earlier copies half-restored.
    for (const auto& [uid, saved] : decoded->copies)
    {
        const auto found = moduleCopies.find(uid);
        const bool reuse = found != moduleCopies.end()
            && found->second->type == saved.slot.type && found->second->range == saved.band;
        auto copy = reuse ? found->second
            : makeModuleCopy(saved.slot.type, saved.band, uid, saved.slot.created);
        if (copy == nullptr) return false;
        nextCopies.emplace(uid, std::move(copy));
    }
    for (const auto& [uid, saved] : decoded->copies)
    {
        auto& copy = nextCopies.at(uid);
        juce::MemoryBlock current;
        if (const auto found = moduleCopies.find(uid); found != moduleCopies.end()
            && found->second == copy)
        {
            if (copy->blocks) copy->blocks->getStateInformation(current);
            else if (copy->dyn) copy->dyn->getStateInformation(current);
            else if (copy->eql) copy->eql->getStateInformation(current);
        }
        if (current.toBase64Encoding() != saved.encodedState)
        {
            juce::MemoryBlock binary;
            binary.fromBase64Encoding(saved.encodedState);
            bool restored = false;
            if (copy->blocks) restored = copy->blocks->setStateInformation(binary.getData(), int(binary.getSize()));
            else if (copy->dyn) restored = copy->dyn->setStateInformation(binary.getData(), int(binary.getSize()));
            else if (copy->eql) restored = copy->eql->setStateInformation(binary.getData(), int(binary.getSize()));
            if (!restored)
            {
                juce::Logger::writeToLog("AVA: cannot update module copy " + juce::String(uid));
                return false;
            }
        }
        copy->serial = saved.slot.created;
    }

    // Observers and slot targets must be detached before removed UIDs die.
    // No original module is recreated, and unchanged copies keep their UID.
    const bool copySetChanged = [&]
    {
        if (nextCopies.size() != moduleCopies.size()) return true;
        for (const auto& [uid, copy] : nextCopies)
            if (auto it = moduleCopies.find(uid); it == moduleCopies.end() || it->second != copy)
                return true;
        return false;
    }();
    if (copySetChanged)
    {
        const auto selected = selectedModuleCopy.load();
        if (selected >= ava::module_stack::originalCount)
        {
            const auto updated = nextCopies.find(selected);
            const auto previous = moduleCopies.find(selected);
            if (updated == nextCopies.end()
                || previous == moduleCopies.end()
                || updated->second != previous->second)
                if (auto* editor = getContentEditor())
                    editor->prepareForProcessorStateRestore();
        }
        invalidateHostSlotTargets();
        clearLoadedModuleStateListeners();
    }
    for (size_t band = 0; band < nextStacks.size(); ++band)
        moduleStacks[band] = nextStacks[band];
    for (size_t band = 0; band < moduleStacks.size(); ++band)
        for (const auto& slot : moduleStacks[band])
            registerModuleSlotBypassTarget(band, slot.uid, slot.type);
    for (size_t band = 0; band < nextCreatedNumbers.size(); ++band)
        lastCreatedModuleNumbers[band] = nextCreatedNumbers[band];
    moduleCopies = std::move(nextCopies);
    nextModuleCopyUid = nextId;
    if (selectedModuleCopy.load() >= ava::module_stack::originalCount
        && !moduleCopies.contains(selectedModuleCopy.load()))
        selectedModuleCopy.store(-1);
    return true;
}
