#include "Processor.h"
#include "Editor.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "ModuleCopyTargets.h"
#include "../routing/State.h"

#include <algorithm>
#include <set>
#include "../crossover/ParameterAccess.h"

namespace {
using Slot=ava::module_stack::Slot;

juce::var asObject() { return juce::var(new juce::DynamicObject()); }
void put(juce::var& object, const juce::Identifier& name, const juce::var& value) {
    if(auto* ptr=object.getDynamicObject())ptr->setProperty(name,value);
}
int number(const juce::var& o,const char* key,int fallback=-1) {
    const auto* obj=o.getDynamicObject();
    if(!obj || !obj->hasProperty(key))return fallback;
    const auto v=obj->getProperty(key);
    return v.isInt() ? int(v) : fallback;
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
    // ADD creates an independent module with factory parameter values.
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

int AvaAudioProcessor::addModuleCopy(size_t band,int sourceUid)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    if(band>=moduleStacks.size()||moduleStacks[band].size()>=ava::module_stack::maximumSlotsPerBand)return -1;
    const auto* slot=ava::module_stack::find(moduleStacks[band],sourceUid);
    if(slot==nullptr)return -1;
    const auto sourceType=slot->type;
    const auto sourceCopy=sourceUid>=ava::module_stack::originalCount
        ? moduleCopies.find(sourceUid) : moduleCopies.end();
    if(sourceUid>=ava::module_stack::originalCount && sourceCopy==moduleCopies.end())return -1;
    const auto id=nextModuleCopyUid;
    // Also account for the serials already present in the stack, so a copy
    // can never be assigned the original module's serial (1).
    const auto serial=ava::module_stack::nextSerial(
        moduleStacks[band], sourceType, lastCreatedModuleNumbers[band][size_t(sourceType)]);
    auto copy=makeModuleCopy(sourceType,int(band),id,serial);
    if(copy==nullptr||!ava::module_stack::insertCopy(moduleStacks[band],sourceUid,id,serial))return -1;
    nextModuleCopyUid++;
    lastCreatedModuleNumbers[band][size_t(sourceType)]=serial;
    moduleCopies.emplace(id,std::move(copy));
    registerLoadedModuleStateListeners();
    publishRenderGraph();
    updateShellLatency();
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
    // Never delete the only remaining instance of a module type.
    if (std::count_if(moduleStacks[band].begin(), moduleStacks[band].end(),
                      [type](const auto& slot) { return slot.type == type; }) <= 1)
        return false;
    if (!ava::module_stack::remove(moduleStacks[band], uid)) return false;
    if(uid>=ava::module_stack::originalCount) {
        // Slots and module-state observers may point to the copied APVTS.
        // Both listener sets must be detached before destroying the copy.
        invalidateHostSlotTargets();
        clearLoadedModuleStateListeners();
        if(selectedModuleCopy.load()==uid)selectedModuleCopy.store(-1);
        moduleCopies.erase(uid);
        // Explicit user deletion is different from temporary absence while
        // REAPER restores a freeze snapshot. Only the former removes slots.
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
            if (ava::module_copy_targets::parse(local, copyUid, copyParameter) && copyUid == uid)
                owner.clearHostSlotAssignment(index);
        }
    } else if (module_order::isChain(type) && blockChainModuleProcessor) {
        const auto& description=ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(type))];
        auto& state=blockChainModuleProcessor->getValueTreeState();
        const auto paramId=ava::crossover::parameters::makeRangeParameterId(band,description.hiddenSuffix);
        if(auto* parameter=state.getParameter(paramId))parameter->setValueNotifyingHost(1.0f);
    } else if(uid==module_order::eql || uid==module_order::dyn) {
        setModuleHiddenForRange(band,uid,true);
    }
    registerLoadedModuleStateListeners();
    publishRenderGraph();
    refreshHostSlotTargets();
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
    // Legacy original blocks also have a hidden flag. Keep it in sync with
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
            put(value,"created",slot.created);put(value,"enabled",slot.enabled);
            slots.add(value);
        }
        juce::Array<juce::var> serials;
        for(int type=0;type<ava::module_stack::originalCount;++type)
            serials.add(lastCreatedModuleNumbers[band][size_t(type)]);
        juce::var addedCreated=asObject();
        for(int type=ava::module_stack::originalCount;type<ava::module_stack::typeCount;++type)
            put(addedCreated,juce::Identifier(juce::String(type)),lastCreatedModuleNumbers[band][size_t(type)]);
        put(range,"addedCreated",addedCreated);
        put(range,"slots",juce::var(slots));
        put(range,"lastCreated",juce::var(serials));
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
        // Legacy state without a stack: only reset stack/copies, not CHAIN,
        // DYN or EQL. Existing original processors remain untouched.
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

    const auto data = juce::JSON::parse(state.getProperty(moduleStackStateKey).toString());
    const auto* root = data.getDynamicObject();
    if (root == nullptr) return false;
    const auto bandsData = root->getProperty("bands");
    const auto copiesData = root->getProperty("copies");
    const auto* bands = bandsData.getArray();
    const auto* copies = copiesData.getArray();
    if (bands == nullptr || copies == nullptr || bands->size() != int(moduleStacks.size())) return false;
    const auto nextId = number(data, "nextId");
    if (nextId < ava::module_stack::originalCount || nextId > 1000000) return false;

    auto nextStacks = moduleStacks;
    auto nextCreatedNumbers = lastCreatedModuleNumbers;
    std::map<int, std::pair<int, Slot>> requestedCopies;
    for (size_t band = 0; band < moduleStacks.size(); ++band)
    {
        const auto* object = (*bands)[int(band)].getDynamicObject();
        if (object == nullptr) return false;
        const auto entriesData = object->getProperty("slots");
        const auto serialsData = object->getProperty("lastCreated");
        const auto* entries = entriesData.getArray();
        const auto* serials = serialsData.getArray();
        if (entries == nullptr || serials == nullptr
            || entries->size() > ava::module_stack::maximumSlotsPerBand
            || serials->size() != ava::module_stack::originalCount) return false;
        ava::module_stack::Order order;
        std::set<int> idsInBand;
        for (const auto& entry : *entries)
        {
            const int uid = number(entry, "id");
            const int type = number(entry, "type");
            const int created = number(entry, "created");
            const auto* slotObject = entry.getDynamicObject();
            if ((uid < 0 && uid != ava::module_stack::originalUid(module_order::sibil)) || uid >= nextId || type < 0 || type >= ava::module_stack::typeCount
                || created < 1 || created > 1000000 || slotObject == nullptr
                || !slotObject->getProperty("enabled").isBool()
                || (uid < ava::module_stack::originalCount && (uid != ava::module_stack::originalUid(type) || created != 1))
                || !idsInBand.insert(uid).second) return false;
            Slot slot { uid, type, created, bool(slotObject->getProperty("enabled")) };
            order.push_back(slot);
            if (uid >= ava::module_stack::originalCount
                && !requestedCopies.emplace(uid, std::make_pair(int(band), slot)).second)
                return false;
        }
        // Every module type has at least one instance in the current model.
        for (int type=0;type<ava::module_stack::typeCount;++type)
            if (std::none_of(order.begin(),order.end(),[type](const auto& slot){return slot.type==type;}))
                order.push_back({ava::module_stack::originalUid(type),type,1,true});
        nextStacks[band] = std::move(order);
        for (int type = 0; type < ava::module_stack::typeCount; ++type)
        {
            const auto added = object->getProperty("addedCreated");
            const auto serial = type < serials->size() ? (*serials)[type]
                : added.getProperty(juce::Identifier(juce::String(type)),1);
            if (!serial.isInt() || int(serial) < 1 || int(serial) > 1000000) return false;
            nextCreatedNumbers[band][size_t(type)] = int(serial);
        }
    }
    if (copies->size() != int(requestedCopies.size())
        || copies->size() > int(moduleStacks.size() * ava::module_stack::maximumSlotsPerBand))
        return false;

    // Stage additions before touching live stacks. Reuse the old processor
    // whenever its UID, band and module type are unchanged.
    std::map<int, std::shared_ptr<ModuleCopy>> nextCopies;
    std::set<int> decoded;
    for (const auto& entry : *copies)
    {
        const auto uid = number(entry, "id");
        const auto requested = requestedCopies.find(uid);
        const auto* object = entry.getDynamicObject();
        if (object == nullptr || requested == requestedCopies.end()
            || !decoded.insert(uid).second) return false;
        const auto& [band, slot] = requested->second;
        const auto found = moduleCopies.find(uid);
        const bool reuse = found != moduleCopies.end()
            && found->second->type == slot.type && found->second->range == band;
        auto copy = reuse ? found->second : makeModuleCopy(slot.type, band, uid, slot.created);
        if (copy == nullptr) return false;

        const auto encoded = object->getProperty("state").toString();
        juce::MemoryBlock binary;
        if (!binary.fromBase64Encoding(encoded)) return false;
        juce::MemoryBlock current;
        if (reuse)
        {
            if (copy->blocks) copy->blocks->getStateInformation(current);
            else if (copy->dyn) copy->dyn->getStateInformation(current);
            else if (copy->eql) copy->eql->getStateInformation(current);
        }
        if (!reuse || current.toBase64Encoding() != encoded)
        {
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
        copy->serial = slot.created;
        nextCopies.emplace(uid, std::move(copy));
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
    moduleStacks = std::move(nextStacks);
    lastCreatedModuleNumbers = std::move(nextCreatedNumbers);
    moduleCopies = std::move(nextCopies);
    nextModuleCopyUid = nextId;
    if (selectedModuleCopy.load() >= ava::module_stack::originalCount
        && !moduleCopies.contains(selectedModuleCopy.load()))
        selectedModuleCopy.store(-1);
    return true;
}
