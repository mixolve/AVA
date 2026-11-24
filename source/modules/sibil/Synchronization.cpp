#include "Synchronization.h"
#include "../../shell/Processor.h"
#include "../../shell/ChoiceControl.h"
#include "../shared/BlockChainProcessor.h"
#include "../../routing/State.h"
#include "../../crossover/ParameterAccess.h"
#include <array>
#include <map>
#include <mutex>
#include <numeric>

namespace {
constexpr std::array<const char*,6> suffixes {
    "sibil.level", "sibil.brightness", "sibil.noise", "sibil.fade",
    ava::modules::sibil::lookaheadId, ava::modules::sibil::holdId};
using Values=std::array<float,suffixes.size()>;
struct Endpoint {
    int node=0,uid=-1;size_t band=0;
    juce::String key,label;
    std::shared_ptr<AvaAudioProcessor> owner;
    std::shared_ptr<BlockChainProcessor> chain;
    Values values() const {
        Values result{};auto& state=chain->getValueTreeState();
        for(size_t i=0;i<suffixes.size();++i)
            if(auto* p=state.getParameter(ava::crossover::parameters::makeRangeParameterId(band,suffixes[i])))result[i]=p->getValue();
        return result;
    }
    void apply(const Values& values) const {
        auto& state=chain->getValueTreeState();
        for(size_t i=0;i<suffixes.size();++i)
            if(auto* p=state.getParameter(ava::crossover::parameters::makeRangeParameterId(band,suffixes[i]));p&&std::abs(p->getValue()-values[i])>1.e-7f)
                p->setValueNotifyingHost(values[i]);
    }
    void setLink(const juce::String& target) const {chain->getValueTreeState().state.setProperty(ava::modules::sibil::linkProperty(band),target,nullptr);}
    juce::String link() const {return chain->getValueTreeState().state.getProperty(ava::modules::sibil::linkProperty(band)).toString();}
};
struct SyncState {std::map<juce::String,Values> values;std::map<juce::String,juce::String> links;};
std::mutex statesMutex;
std::map<uint64_t,std::shared_ptr<SyncState>> states;
}

// Capture shared processor owners under their normal model lock. UI rebuilds,
// state restores and module deletion cannot leave the synchroniser dangling.
class SibilLinkAccess {
public:
static std::vector<Endpoint> endpoints(AvaAudioProcessor& root) {
    std::vector<Endpoint> result;
    const auto topology=ava::routing::readState(root.getValueTreeState().state);
    for(const auto& node:topology.nodes) {
        if(node.container)continue;
        auto handle=node.id==topology.rootInstanceId?std::shared_ptr<AvaAudioProcessor>{}:root.getRoutingInstanceHandle(node.id);
        auto* instance=node.id==topology.rootInstanceId?&root:handle.get();if(!instance)continue;
        const juce::ScopedLock lock(instance->processingLock);
        const auto count=size_t(instance->getCrossoverSettings().activeSplitCount+1);
        for(size_t band=0;band<count&&band<instance->moduleStacks.size();++band) {
            const auto& stack=instance->moduleStacks[band];
            for(const auto& slot:stack) {
                if(slot.type!=module_order::sibil||!ava::module_stack::processingActive(slot))continue;
                std::shared_ptr<BlockChainProcessor> chain;
                if(slot.uid<ava::module_stack::originalCount)chain=instance->blockChainModuleProcessor;
                else if(auto it=instance->moduleCopies.find(slot.uid);it!=instance->moduleCopies.end())chain=it->second->blocks;
                if(!chain)continue;
                const auto* hidden=chain->getValueTreeState().getRawParameterValue(ava::crossover::parameters::makeRangeParameterId(band,"sibil.hidden"));
                if(!hidden||hidden->load()>=.5f)continue;
                const auto key=juce::String(node.id)+":"+juce::String(int(band))+":"+juce::String(slot.uid);
                const auto name=ava::routing::getDisplayName(topology,node.id);
                const auto label=name.isNotEmpty()?name:"INS "+juce::String(node.id);
                result.push_back({node.id,slot.uid,band,key,label,handle,chain});
            }
        }
    }
    return result;
}
};

namespace ava::modules::sibil {
juce::Identifier linkProperty(size_t band){return juce::Identifier("ava.sbl.sync.band-"+juce::String(int(band+1)));}
namespace {
void selectLink(BlockChainProcessor& chain,size_t band,const juce::String& target) {
    auto* owner=dynamic_cast<AvaAudioProcessor*>(&chain.getOwnerProcessor());if(!owner)return;
    const auto endpoints=SibilLinkAccess::endpoints(owner->getRoutingOwner());
    const auto source=std::find_if(endpoints.begin(),endpoints.end(),[&](const auto& e){return e.chain.get()==&chain&&e.band==band;});
    if(source==endpoints.end()){chain.getValueTreeState().state.setProperty(linkProperty(band),target,nullptr);return;}
    // A selector represents one mutual pair. Disconnect either side together.
    const auto disconnect=[&](const juce::String& key){for(const auto& e:endpoints)if(e.key==key||e.link()==key)e.setLink({});};
    disconnect(source->key);
    const auto peer=std::find_if(endpoints.begin(),endpoints.end(),[&](const auto& e){return e.key==target&&e.node!=source->node;});
    if(peer!=endpoints.end()) {
        disconnect(peer->key);
        source->apply(peer->values());
        source->setLink(peer->key);peer->setLink(source->key);
    }
}
class LinkControl final:public juce::Component,private juce::Timer {
public:
    LinkControl(BlockChainProcessor& processor,size_t bandIn):chain(processor),band(bandIn),choice("SYNC",{"NONE"},0) {
        getProperties().set("disableDockSelection",true);
        choice.setBottomIndicatorsVisible(false,false);
        choice.getProperties().set("disableDockSelection",true);
        choice.onValueChanged=[this]{const int i=choice.getSelectedChoiceIndex();if(i>=0&&i<keys.size())selectLink(chain,band,keys[i]);};
        addAndMakeVisible(choice);timerCallback();startTimerHz(5);
    }
    void resized() override {choice.setBounds(getLocalBounds());}
private:
    void timerCallback() override {
        if(getParentComponent()!=nullptr&&!isShowing())return;
        auto* owner=dynamic_cast<AvaAudioProcessor*>(&chain.getOwnerProcessor());if(!owner)return;
        auto& root=owner->getRoutingOwner();
        const auto endpoints=SibilLinkAccess::endpoints(root);
        const auto selected=chain.getValueTreeState().state.getProperty(linkProperty(band)).toString();
        std::map<int,const Endpoint*> options;
        for(const auto& peer:endpoints)if(peer.node!=owner->getRoutingInstanceId()) {
            const auto found=options.find(peer.node);
            if(found==options.end()||peer.key==selected
                ||(found->second->key!=selected&&peer.band==band&&found->second->band!=band))options[peer.node]=&peer;
        }
        juce::StringArray nextKeys{juce::String()},labels{"NONE"};
        for(const auto& [node,peer]:options){juce::ignoreUnused(node);nextKeys.add(peer->key);labels.add(peer->label);}
        if(selected.isNotEmpty()&&!nextKeys.contains(selected)) {
            const auto node=selected.upToFirstOccurrenceOf(":",false,false).getIntValue();
            const auto topology=ava::routing::readState(root.getValueTreeState().state);
            const auto name=ava::routing::getDisplayName(topology,node);
            nextKeys.add(selected);labels.add(name.isNotEmpty()?name:"INS "+juce::String(node));
        }
        if(keys!=nextKeys||choice.getChoiceItems()!=labels){keys=nextKeys;choice.setChoices(labels);}
        choice.setSelectedChoiceIndex(juce::jmax(0,keys.indexOf(selected)),false);
    }
    BlockChainProcessor& chain;size_t band;
    LocalChoiceControl choice;juce::StringArray keys;
};
}
std::unique_ptr<juce::Component> makeLinkControl(BlockChainProcessor& chain,size_t band){return std::make_unique<LinkControl>(chain,band);}
void forgetRouting(uint64_t root){std::lock_guard lock(statesMutex);states.erase(root);}
void syncRouting(AvaAudioProcessor& root) {
    std::shared_ptr<SyncState> previous;
    {std::lock_guard lock(statesMutex);auto& state=states[root.getLinkSessionId()];if(!state)state=std::make_shared<SyncState>();previous=state;}
    auto endpoints=SibilLinkAccess::endpoints(root);
    std::map<juce::String,size_t> indices;
    std::vector<size_t> parent(endpoints.size());std::iota(parent.begin(),parent.end(),size_t(0));
    const auto find=[&](size_t i){while(parent[i]!=i)i=parent[i];return i;};
    for(size_t i=0;i<endpoints.size();++i)indices.emplace(endpoints[i].key,i);
    // A restored one-sided selection also activates the receiving selector.
    for(const auto& source:endpoints)if(const auto peer=indices.find(source.link());peer!=indices.end()
        &&endpoints[peer->second].node!=source.node&&endpoints[peer->second].link().isEmpty()) {
        endpoints[peer->second].setLink(source.key);
        previous->links[endpoints[peer->second].key]=source.key;
    }
    std::map<juce::String,juce::String> links;
    std::vector<std::pair<size_t,size_t>> joined;
    for(size_t i=0;i<endpoints.size();++i){auto target=endpoints[i].link();links[endpoints[i].key]=target;
        const auto peer=indices.find(target);if(peer==indices.end()||endpoints[peer->second].node==endpoints[i].node)continue;
        parent[find(i)]=find(peer->second);
        if(!previous->links.contains(endpoints[i].key)||previous->links[endpoints[i].key]!=target)joined.emplace_back(i,peer->second);
    }
    std::map<size_t,std::vector<size_t>> groups;
    for(size_t i=0;i<endpoints.size();++i)groups[find(i)].push_back(i);
    std::vector<Values> current;for(const auto& e:endpoints)current.push_back(e.values());
    for(const auto& [group,members]:groups){
        if(members.size()<2)continue;
        auto wanted=current[members.front()];bool joining=false;
        for(const auto& [source,target]:joined)if(find(source)==group){wanted=current[target];joining=true;}
        if(!joining)for(size_t p=0;p<suffixes.size();++p)
            for(auto i:members)if(auto it=previous->values.find(endpoints[i].key);it!=previous->values.end()&&std::abs(current[i][p]-it->second[p])>1.e-7f)wanted[p]=current[i][p];
        for(auto i:members){endpoints[i].apply(wanted);current[i]=wanted;}
    }
    previous->links=std::move(links);previous->values.clear();
    for(size_t i=0;i<endpoints.size();++i)previous->values.emplace(endpoints[i].key,current[i]);
}
}
