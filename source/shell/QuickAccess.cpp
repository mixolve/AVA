#include "Editor.h"
#include "ValueEditor.h"
#include "EditorStateSchema.h"
#include "../routing/State.h"
#include "EditorSetup.h"
#include "ChoiceControl.h"
#include "FilterSection.h"
#include "EditorPreferences.h"
#include "FilterOrderState.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/shared/BlockParameters.h"
#include "../crossover/ParameterIds.h"
#include "../crossover/ModuleBindings.h"
#include "../modules/dyn/Processor.h"
#include "../routing/Panel.h"
#include "PresetSection.h"
#include "ParameterControl.h"
#include "TextControl.h"
#include <map>

namespace
{
// Each copied module has its own APVTS but reuses its original's parameter
// IDs. Include the stable copy UID in dock keys, not just the parameter ID.
struct DockModuleIdentity
{
    int uid = -1;
    int band = -1;
    int created = -1;
};

// These controls always belong to the root UNIT APVTS, regardless of which
// routing INSTANCE happens to show its UNIT page.
bool isUnitWideDockParameter(const juce::String& id)
{
    return id == AvaAudioProcessor::paramGlobalBypassId
        || id == AvaAudioProcessor::paramBypassCompensationId;
}

bool isUnitWideDockControl(const juce::Component& control)
{
    if (auto* parameter = dynamic_cast<const ParameterControl*>(&control))
        return isUnitWideDockParameter(parameter->getParameterId());
    return false;
}

bool isUnitWideDockMetadata(const juce::var& metadata)
{
    return isUnitWideDockParameter(metadata.getProperty("id", {}).toString());
}

// Filter-parameter IDs are EQL-specific and remain the same for original
// filters, copied EQL modules and routing instances. Remove any stale filter
// pins to prevent them from reappearing after an editor refresh.
void removeEqlFilterDockPins(juce::ValueTree& dockState)
{
    const auto orderKey = shell_editor_ui::key("quick_access");
    const auto raw = dockState.getProperty(orderKey).toString();
    auto ordered = juce::StringArray::fromTokens(raw, "|", "");
    for (int index = ordered.size(); --index >= 0;)
    {
        const auto& id = ordered[index];
        const auto metadataKey = shell_editor_ui::key("dock.meta." + id);
        const auto metadata = juce::JSON::parse(dockState.getProperty(metadataKey).toString());
        const auto parameterId = metadata.getProperty("id", {}).toString();
        const auto auxiliaryId = metadata.getProperty("auxiliaryId", {}).toString();
        if (!parameterId.startsWith("filter-") && !auxiliaryId.startsWith("filter-"))
            continue;
        dockState.removeProperty(shell_editor_ui::key("selected." + id), nullptr);
        dockState.removeProperty(metadataKey, nullptr);
        ordered.remove(index);
    }
    if (ordered.joinIntoString("|") != raw)
        dockState.setProperty(orderKey, ordered.joinIntoString("|"), nullptr);
}

DockModuleIdentity dockModuleIdentity(const juce::Component& control)
{
    for (auto* part = &control; part != nullptr; part = part->getParentComponent())
    {
        const auto slotUid = int(part->getProperties().getWithDefault("moduleSlotUid", -1));
        if (slotUid != -1)
            return {slotUid,
                int(part->getProperties().getWithDefault("moduleSlotBand", -1)),
                int(part->getProperties().getWithDefault("moduleSlotCreated", 1))};
        const auto uid = int(part->getProperties().getWithDefault("moduleCopyUid", -1));
        if (uid >= ava::module_stack::originalCount)
            return {uid, int(part->getProperties().getWithDefault("moduleCopyBand", -1)), -1};
    }
    return {};
}

// A saved dock tile must be reconstructable when the editor which originally
// supplied it has been closed. All UI-only metadata lives in the owner's state;
// the sound parameter itself remains in its owning routing instance.
const AccessoryButton* dockEmbeddedAccessory(const juce::Component& source)
{
    if (dynamic_cast<const ParameterField*>(&source) == nullptr) return nullptr;
    for (auto* child : source.getChildren())
        if (auto* accessory = dynamic_cast<const AccessoryButton*>(child))
            return accessory;
    return nullptr;
}

juce::Identifier dockMetadataKey(const juce::String& id)
{
    return shell_editor_ui::key("dock.meta." + id);
}

juce::var dockMetadata(const juce::Component& source, int instanceId, int selectedBand)
{
    const auto module = dockModuleIdentity(source);
    auto* data = new juce::DynamicObject();
    data->setProperty("instance", instanceId);
    data->setProperty("uid", module.uid);
    data->setProperty("created", module.created);
    data->setProperty("band", module.band >= 0 ? module.band : selectedBand);
    if (const auto* param = dynamic_cast<const ParameterControl*>(&source))
    {
        data->setProperty("kind", "parameter");
        data->setProperty("id", param->getParameterId());
        data->setProperty("title", param->getTitleText());
        data->setProperty("decimals", param->getEditorDecimals());
        if (auto* state = param->getBoundState())
            data->setProperty("state", state->state.getType().toString());
    }
    else if (const auto* choice = dynamic_cast<const ChoiceControl*>(&source))
    {
        data->setProperty("kind", "choice");
        data->setProperty("id", choice->getParameterId());
        data->setProperty("title", choice->getTitleText());
        if (auto* state = choice->getBoundState())
            data->setProperty("state", state->state.getType().toString());
    }
    else if (const auto* accessory = dynamic_cast<const AccessoryButton*>(&source))
    {
        data->setProperty("kind", "accessory");
        data->setProperty("id", accessory->getParameterId());
        data->setProperty("title", accessory->getButtonText());
        if (auto* state = accessory->getBoundState())
            data->setProperty("state", state->state.getType().toString());
    }
    else data->setProperty("kind", "action");
    if (const auto* field = dynamic_cast<const ParameterField*>(&source))
    {
        data->setProperty("range", field->areRangeIndicatorsVisible());
        data->setProperty("bottom0", field->isBottomIndicatorVisible(0));
        data->setProperty("bottom1", field->isBottomIndicatorVisible(1));
        if (const auto* accessory = dockEmbeddedAccessory(*field))
        {
            data->setProperty("auxiliaryId", accessory->getParameterId());
            data->setProperty("auxiliaryTitle", accessory->getButtonText());
            if (auto* state = accessory->getBoundState())
                data->setProperty("auxiliaryState", state->state.getType().toString());
        }
    }
    return juce::var(data);
}

juce::AudioProcessorValueTreeState* dockParameterState(AvaAudioProcessor& processor,
                                                        const juce::var& metadata,
                                                        const juce::String& auxiliaryId = {},
                                                        const juce::String& auxiliaryState = {})
{
    const auto uid = int(metadata.getProperty("uid", -1));
    const auto band = int(metadata.getProperty("band", -1));
    const auto stateType = auxiliaryState.isNotEmpty() ? auxiliaryState
        : metadata.getProperty("state", {}).toString();
    const auto parameterId = auxiliaryId.isNotEmpty() ? auxiliaryId
        : metadata.getProperty("id", {}).toString();
    if (parameterId.isEmpty()) return nullptr;
    auto matching = [&](juce::AudioProcessorValueTreeState* state)
    {
        return state != nullptr && state->state.getType().toString() == stateType
            && state->getParameter(parameterId) != nullptr ? state : nullptr;
    };
    if (uid >= ava::module_stack::originalCount)
    {
        // An INSTANCE can reuse another INSTANCE's module UID: the owning
        // processor is resolved first, before considering that local UID.
        if (auto* chain = processor.getCopiedBlockChainProcessor(uid))
            if (auto* result = matching(&chain->getValueTreeState())) return result;
        if (auto* dyn = processor.getCopiedDynProcessor(uid))
            if (auto* result = matching(&dyn->getValueTreeState())) return result;
        if (band >= 0)
            if (auto* eql = processor.getCopiedEqlProcessor(uid, size_t(band)))
                if (auto* result = matching(&eql->getValueTreeState())) return result;
        return nullptr;
    }
    if (auto* result = matching(&processor.getValueTreeState())) return result;
    if (auto* chain = processor.getBlockChainProcessor())
        if (auto* result = matching(&chain->getValueTreeState())) return result;
    if (auto* dyn = processor.getDynModuleProcessor())
        if (auto* result = matching(&dyn->getValueTreeState())) return result;
    if (band >= 0)
        if (auto* bank = processor.getEqlProcessorBank())
            if (auto* eql = bank->getProcessor(size_t(band)))
                if (auto* result = matching(&eql->getValueTreeState())) return result;
    if (auto* eql = processor.getEqlModuleProcessor())
        if (auto* result = matching(&eql->getValueTreeState())) return result;
    // UNIT-level controls such as global BYPASS belong to the routing owner,
    // including when they were pinned from a nested instance's UNIT page.
    if (&processor != &processor.getRoutingOwner())
        if (auto* result = matching(&processor.getRoutingOwner().getValueTreeState())) return result;
    return nullptr;
}

// A dock pin is owned by the INSTANCE and module recorded in its metadata,
// not by whatever INSTANCE happens to be visible when this editor refreshes.
// Never mirror a different module's identically named parameter.
bool matchesDockSourceOwner(const juce::Component* source, const juce::var& metadata,
                            AvaAudioProcessor& routingOwner, const int editorInstanceId)
{
    if (source == nullptr) return false;
    if (metadata.isVoid()) return false;
    const auto kind = metadata.getProperty("kind", {}).toString();
    const auto topology = ava::routing::readState(routingOwner.getValueTreeState().state);
    const auto unitWide = isUnitWideDockMetadata(metadata);
    const auto ownerId = unitWide ? topology.rootInstanceId
        : int(metadata.getProperty("instance", topology.rootInstanceId));
    // UI-only buttons cannot be bound through APVTS. A live callback must
    // belong to the editor that supplied it, not the currently selected peer.
    if (kind != "parameter" && kind != "choice" && kind != "accessory")
        return unitWide || ownerId == (editorInstanceId != 0 ? editorInstanceId : topology.rootInstanceId);
    auto* processor = &routingOwner;
    std::shared_ptr<AvaAudioProcessor> instanceHandle;
    if (ownerId != topology.rootInstanceId)
    {
        instanceHandle = routingOwner.getRoutingInstanceHandle(ownerId);
        if (instanceHandle == nullptr) return false;
        processor = instanceHandle.get();
    }
    const auto* expected = dockParameterState(*processor, metadata);
    if (expected == nullptr) return false;
    const auto parameterId = metadata.getProperty("id", {}).toString();
    if (const auto* parameter = dynamic_cast<const ParameterControl*>(source))
        return parameter->getBoundState() == expected && parameter->getParameterId() == parameterId;
    if (const auto* choice = dynamic_cast<const ChoiceControl*>(source))
        return choice->getBoundState() == expected && choice->getParameterId() == parameterId;
    if (const auto* accessory = dynamic_cast<const AccessoryButton*>(source))
        return accessory->getBoundState() == expected && accessory->getParameterId() == parameterId;
    return false;
}

// A bottom-row accessory is a genuine APVTS parameter. Display it as an
// independent full-size dock tile, keeping the same APVTS attachment when the
// originating editor is closed or another routing INSTANCE is selected.
std::unique_ptr<juce::Component> makeDockAccessoryControl(
    juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
    const juce::String& title)
{
    auto* parameter = state.getParameter(parameterId);
    if (parameter == nullptr) return {};
    if (dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr)
        return std::make_unique<ChoiceControl>(state, parameterId, title);
    return std::make_unique<ParameterControl>(state, parameterId, title, 0);
}

struct DockModuleStatus
{
    bool identified = false;
    bool enabled = true;
    int created = -1;
    int ordinal = 0;
};

DockModuleStatus dockModuleStatus(AvaAudioProcessor& processor, const juce::var& metadata)
{
    const auto uid = int(metadata.getProperty("uid", -1));
    if (uid == -1) return {}; // UNIT control, not a module in the signal path.
    const auto band = int(metadata.getProperty("band", -1));
    if (band < 0) return {true, false, -1};
    const auto slots = processor.getModuleStackForRange(size_t(band));
    const auto* slot = ava::module_stack::find(slots, uid);
    if (slot == nullptr) return {true, false, -1};
    bool enabled = slot->enabled;
    if (enabled && uid < ava::module_stack::originalCount)
    {
        if (!module_order::isChain(slot->type))
            enabled = !processor.isModuleHiddenForRange(size_t(band), slot->type);
        else if (auto* chain = processor.getBlockChainProcessor())
        {
            const auto id = ava::crossover::parameters::makeRangeParameterId(size_t(band),
                ava::modules::chain::blocks::descriptions[size_t(module_order::chainIndex(slot->type))].hiddenSuffix);
            if (auto* hidden = chain->getValueTreeState().getParameter(id))
                enabled = hidden->getValue() < 0.5f;
        }
    }
    return {true, enabled, slot->created, ava::module_stack::displayOrdinal(slots, uid)};
}

juce::String dockBadgeNumber(const DockModuleStatus& module, int routingInstanceId)
{
    return juce::String(module.identified && module.ordinal > 0 ? module.ordinal
        : module.identified ? 1 : routingInstanceId).paddedLeft('0', 2);
}

}

juce::Identifier AvaAudioProcessorEditor::controlSelectionStateKey(const juce::Component& source) const
{
    for (auto* parent = &source; parent != nullptr; parent = parent->getParentComponent())
        if (bool(parent->getProperties().getWithDefault("disableDockSelection", false))) return {};
    const juce::Component* control = &source;
    if (!bool(source.getProperties().getWithDefault("independentDockSelection", false)))
        if (const auto* field = source.findParentComponentOfClass<ParameterField>()) control = field;
    const auto explicitKey = control->getProperties()["quickAccessSelectionKey"].toString();
    if (explicitKey.isNotEmpty()) return juce::Identifier(explicitKey);
    juce::String identity;
    if (const auto* parameter = dynamic_cast<const ParameterControl*>(control)) identity = parameter->getParameterId();
    else if (const auto* choice = dynamic_cast<const ChoiceControl*>(control)) identity = choice->getParameterId();
    else identity = control->getProperties()["oscParameterId"].toString();
    if (identity.startsWith("filter-")) return {}; // EQL filter parameters cannot be docked.
    if (identity.isNotEmpty())
    {
        identity = "parameter:" + identity;
    }
    else
    {
        auto label = control->getComponentID();
        if (label.isEmpty()) label = control->getName();
        if (label.isEmpty())
            if (const auto* field = dynamic_cast<const ParameterField*>(control)) label = field->getTitleText();
        if (label.isEmpty())
            if (const auto* button = dynamic_cast<const juce::Button*>(control)) label = button->getButtonText();
        if (label.isEmpty())
            if (const auto* readout = dynamic_cast<const SquareTextLabel*>(control)) label = readout->getText();
        if (label.isEmpty()) return {};
        juce::String scope = "shell";
        for (auto* parent = control->getParentComponent(); parent != nullptr && parent != this; parent = parent->getParentComponent())
            if (parent->getName().startsWith("module-") || parent->getName() == "about")
            { scope = parent->getName(); break; }
        identity = scope + ":" + label;
    }
    // Copies of CHAIN/DYN/EQL in the same BAND otherwise collapse into
    // one marked control; a UID stays stable after reordering the stack.
    const auto module = dockModuleIdentity(*control);
    if (module.uid >= ava::module_stack::originalCount)
        identity = "module-uid:" + juce::String(module.uid)
            + ":band:" + juce::String(module.band) + ":" + identity;
    // Routing instances have their own processors and repeat parameter IDs
    // and module-copy UIDs. Qualify the identity by the routing node first.
    if (routingInstanceId != 0 && !isUnitWideDockControl(*control))
        identity = "routing-instance:" + juce::String(routingInstanceId) + ":" + identity;
    return shell_editor_ui::key("selected." + juce::String::toHexString(
        identity.toRawUTF8(), int(identity.getNumBytesAsUTF8()), 0));
}

bool AvaAudioProcessorEditor::isControlManuallySelected(const juce::Component& component) const
{
    const auto key = controlSelectionStateKey(component);
    return key.isValid() && bool(audioProcessor.getRoutingOwner().getValueTreeState().state.getProperty(key, false));
}

void AvaAudioProcessorEditor::toggleControlManualSelection(juce::Component& component)
{
    const auto key = controlSelectionStateKey(component);
    if (!key.isValid()) return;
    auto& state = audioProcessor.getRoutingOwner().getValueTreeState().state;
    const auto selected = bool(state.getProperty(key, false));
    if (selected) state.removeProperty(key, nullptr);
    else
    {
        state.setProperty(key, true, nullptr);
        auto* source = &component;
        if (!bool(component.getProperties().getWithDefault("independentDockSelection", false)))
            if (auto* field = component.findParentComponentOfClass<ParameterField>()) source = field;
        const auto topology = ava::routing::readState(state);
        const auto instanceId = isUnitWideDockControl(*source) ? topology.rootInstanceId
            : routingInstanceId != 0 ? routingInstanceId : topology.rootInstanceId;
        const auto prefix = shell_editor_ui::key("selected.").toString();
        state.setProperty(dockMetadataKey(key.toString().substring(prefix.length())),
            juce::JSON::toString(dockMetadata(*source, instanceId, int(audioProcessor.getSelectedCrossoverRange())), false), nullptr);
    }

    // Order lives beside the existing Ctrl+click selection flags; neither
    // has a second toggle/selection implementation.
    const auto prefix = shell_editor_ui::key("selected.").toString();
    const auto id = key.toString().substring(prefix.length());
    auto ordered = juce::StringArray::fromTokens(
        state.getProperty(shell_editor_ui::key("quick_access")).toString(), "|", "");
    ordered.removeString(id);
    if (selected) state.removeProperty(dockMetadataKey(id), nullptr);
    else ordered.add(id);
    state.setProperty(shell_editor_ui::key("quick_access"), ordered.joinIntoString("|"), nullptr);

    // The originating control may be destroyed by a mouse-up callback. Rebuild
    // the row after that gesture has completed, never in the mouse-up stack.
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
        {
            if (safe == nullptr) return;
            safe->refreshQuickAccessControls();
            if (auto* root = safe->audioProcessor.getRoutingOwner().getContentEditor())
                if (root != safe.getComponent()) root->refreshQuickAccessControls();
        });
}

void AvaAudioProcessorEditor::layoutQuickAccessStrip()
{
    const auto gap = getUiGap(*this);
    const auto configuredCount = shell_editor_ui::dockControlCount(getRootWindowState());
    const bool showDock = configuredCount > 0;
    const auto side = getSquareControlSize(*this);
    constexpr auto toggleSize = unitIndicatorSize;
    if (!showDock)
    {
        // The content viewport uses the strip's top edge as its lower bound.
        quickAccessStrip.setBounds(0, getHeight() - gap, 0, 0);
        dockClipIndicator.setBounds(getWidth() - gap - toggleSize, getHeight() - gap - toggleSize,
                                    toggleSize, toggleSize);
        return;
    }

    const auto inset = gap;
    const auto height = side + 2 * inset;
    const bool vertical = isDockOnly() && bool(getRootWindowState().getProperty(shell_editor_ui::key("dock_vertical"), false));
    // Use the horizontal dock as the single layout reference. In vertical
    // mode every rectangle is rotated clockwise, including the clip indicator.
    const auto layoutWidth = vertical ? getHeight() : getWidth();
    const auto layoutHeight = vertical ? side + 3 * gap : getHeight();
    const auto rotate = [vertical, layoutHeight](juce::Rectangle<int> r)
    {
        return vertical ? juce::Rectangle<int>(layoutHeight - r.getBottom(), r.getX(), r.getHeight(), r.getWidth()) : r;
    };
    // Reserve the configured slots in a collapsed dock, even when fewer are pinned.
    const auto count = isDockOnly() ? configuredCount : shell_editor_ui::editorColumns(getRootWindowState());
    const auto visibleLength = count * side + (count - 1) * gap;
    const auto expandedStripX = 2 * (gap + controlBlockFrameThickness);
    const auto stripX = vertical ? gap : (isDockOnly() ? 0 : expandedStripX);
    const juce::Rectangle<int> strip(stripX, layoutHeight - height,
        visibleLength + 2 * inset, height);
    const auto dockBounds = strip.reduced(inset);
    const auto rightButtonsX = isDockOnly()
        ? dockBounds.getRight() + gap
        : layoutWidth - gap - toggleSize;
    quickAccessStrip.setBounds(rotate(strip));
    const auto viewportBounds = rotate(dockBounds);
    quickAccessViewport.setBounds(viewportBounds.translated(-quickAccessStrip.getX(), -quickAccessStrip.getY()));
    const auto serviceGap = juce::jmax(0, (dockBounds.getHeight() - 5 * toggleSize) / 3);
    const auto firstY = dockBounds.getY();
    dockClipIndicator.setBounds(rotate({rightButtonsX,
        firstY, toggleSize, toggleSize}));
    dockSortToggle.setBounds(rotate({rightButtonsX,
        firstY + toggleSize + serviceGap, toggleSize, toggleSize}));
    dockCollapseToggle.setBounds(rotate({rightButtonsX,
        firstY + 2 * (toggleSize + serviceGap), toggleSize, toggleSize * 2}));
    dockVerticalToggle.setBounds(rotate({rightButtonsX,
        dockBounds.getBottom() - toggleSize, toggleSize, toggleSize}));
    dockVerticalToggle.setEnabled(isDockOnly());
    dockSortOverlay.setBounds(rotate(dockBounds));
    const auto slotCount = juce::jmax(int(quickAccessItems.size()), configuredCount);
    const auto step = side + gap;
    std::vector<juce::Rectangle<int>> emptySlots;
    for (int index = int(quickAccessItems.size()); index < slotCount; ++index)
        emptySlots.push_back(vertical ? juce::Rectangle<int>(0, index * step, side, side)
                                     : juce::Rectangle<int>(index * step, 0, side, side));
    quickAccessContent.setEmptySlots(std::move(emptySlots));
    if (vertical)
    {
        quickAccessContent.setSize(side, juce::jmax(dockBounds.getWidth(), slotCount * step - gap));
        for (size_t i = 0; i < quickAccessItems.size(); ++i)
            if (auto* control = quickAccessItems[i].control.get()) control->setBounds(0, int(i) * step, side, side);
        quickAccessViewport.setViewPosition(0, juce::jlimit(0, juce::jmax(0, quickAccessContent.getHeight() - quickAccessViewport.getHeight()), quickAccessViewport.getViewPositionY()));
        return;
    }

    const auto width = juce::jmax(quickAccessViewport.getWidth(),
                                 slotCount * step - gap);
    quickAccessContent.setSize(width, side);
    for (size_t index = 0; index < quickAccessItems.size(); ++index)
        if (auto* control = quickAccessItems[index].control.get())
            control->setBounds(int(index) * step, 0, side, side);

    quickAccessViewport.setViewPosition(
        juce::jlimit(0, juce::jmax(0, width - quickAccessViewport.getWidth()),
                     quickAccessViewport.getViewPositionX()), 0);
}

void AvaAudioProcessorEditor::refreshQuickAccessControls()
{
    auto& dockState = audioProcessor.getRoutingOwner().getValueTreeState().state;
    removeEqlFilterDockPins(dockState);
    const auto rawOrder = dockState.getProperty(
        shell_editor_ui::key("quick_access")).toString();
    auto ordered = juce::StringArray::fromTokens(rawOrder, "|", "");
    const auto prefix = shell_editor_ui::key("selected.").toString();
    std::map<juce::String, juce::Component*> sources;
    bool metadataChanged = false;
    const auto scan = [&](const auto& self, juce::Component& component) -> void
    {
        const bool isParameterField = dynamic_cast<ParameterField*>(&component) != nullptr;
        if (isParameterField || dynamic_cast<AccessoryButton*>(&component) != nullptr)
        {
            {
                const auto id = controlSelectionStateKey(component).toString();
                if (id.startsWith(prefix)
                    && bool(dockState.getProperty(juce::Identifier(id), false)))
                {
                    const auto suffix = id.substring(prefix.length());
                    const auto saved = juce::JSON::parse(
                        dockState.getProperty(dockMetadataKey(suffix)).toString());
                    const auto module = dockModuleIdentity(component);
                    const auto* embedded = dockEmbeddedAccessory(component);
                    const auto auxiliaryId = embedded != nullptr ? embedded->getParameterId() : juce::String();
                    if (matchesDockSourceOwner(&component, saved, audioProcessor.getRoutingOwner(), routingInstanceId)
                        && ((module.uid != -1
                         && (int(saved.getProperty("uid", -1)) != module.uid
                             || int(saved.getProperty("created", -1)) != module.created))
                        || (embedded != nullptr
                            && saved.getProperty("auxiliaryId", {}).toString() != auxiliaryId)))
                    {
                        const auto topology = ava::routing::readState(dockState);
                        dockState.setProperty(dockMetadataKey(suffix),
                            juce::JSON::toString(dockMetadata(component,
                                isUnitWideDockControl(component) ? topology.rootInstanceId
                                    : routingInstanceId != 0 ? routingInstanceId : topology.rootInstanceId,
                                int(audioProcessor.getSelectedCrossoverRange())), false), nullptr);
                        metadataChanged = true;
                    }
                    // Prefer the currently visible presentation when a module
                    // has alternate synced/time controls with the same ID.
                    const auto found = sources.find(suffix);
                    if (matchesDockSourceOwner(&component, saved, audioProcessor.getRoutingOwner(), routingInstanceId)
                        && (found == sources.end() || (component.isVisible() && !found->second->isVisible())))
                        sources[suffix] = &component;
                }
            }
            // Embedded auxiliary buttons are selectable parameters in their
            // own right. A field and its accessory may both be pinned.
            if (isParameterField)
                for (auto* child : component.getChildren())
                    if (dynamic_cast<AccessoryButton*>(child) != nullptr)
                        self(self, *child);
            return;
        }
        if (dynamic_cast<BoxTextButton*>(&component) != nullptr
            || dynamic_cast<SquareTextLabel*>(&component) != nullptr)
        {
            {
                const auto id = controlSelectionStateKey(component).toString();
                if (id.startsWith(prefix)
                    && bool(dockState.getProperty(juce::Identifier(id), false)))
                    sources[id.substring(prefix.length())] = &component;
            }
            return;
        }
        for (auto* child : component.getChildren()) self(self, *child);
    };
    scan(scan, moduleContent);
    bool changed = metadataChanged || rawOrder != quickAccessOrderCache;
    if (!changed)
    {
        size_t displayed = 0;
        for (const auto& id : ordered)
        {
            const auto source = sources.find(id);
            const auto metadata = juce::JSON::parse(dockState.getProperty(dockMetadataKey(id)).toString());
            const auto* found = source == sources.end()
                || !matchesDockSourceOwner(source->second, metadata, audioProcessor.getRoutingOwner(), routingInstanceId)
                ? nullptr : source->second;
            if (found == nullptr && metadata.isVoid()) continue;
            if (displayed >= quickAccessItems.size()
                || quickAccessItems[displayed].key != id
                || quickAccessItems[displayed].source.getComponent() != found)
            { changed = true; break; }
            ++displayed;
        }
        if (displayed != quickAccessItems.size()) changed = true;
    }
    if (!changed) return;
    // A floating value editor keeps a SafePointer to its source tile. Do not
    // destroy that tile in the middle of an edit when layout refreshes.
    for (const auto& item : quickAccessItems)
        if (item.control != nullptr
            && bool(item.control->getProperties().getWithDefault("dockValueEditing", false)))
            return;

    // Drop old APVTS attachments before constructing replacements. This is
    // essential when a selected module has been rebound to another BAND.
    quickAccessContent.removeAllChildren();
    quickAccessItems.clear();
    quickAccessOrderCache = rawOrder;
    for (const auto& id : ordered)
    {
        const auto found = sources.find(id);
        const auto metadata = juce::JSON::parse(dockState.getProperty(dockMetadataKey(id)).toString());
        auto* source = found != sources.end()
            && matchesDockSourceOwner(found->second, metadata, audioProcessor.getRoutingOwner(), routingInstanceId)
            ? found->second : nullptr;
        if (source == nullptr && metadata.isVoid()) continue;
        auto& rootProcessor = audioProcessor.getRoutingOwner();
        const auto topology = ava::routing::readState(rootProcessor.getValueTreeState().state);
        const bool unitWide = isUnitWideDockMetadata(metadata);
        const auto instanceId = unitWide ? topology.rootInstanceId
            : int(metadata.getProperty("instance",
                routingInstanceId != 0 ? routingInstanceId : topology.rootInstanceId));
        std::shared_ptr<AvaAudioProcessor> instanceHandle;
        auto* instanceProcessor = &rootProcessor;
        if (instanceId != topology.rootInstanceId)
        {
            instanceHandle = rootProcessor.getRoutingInstanceHandle(instanceId);
            if (instanceHandle == nullptr) continue; // The routing node was deleted.
            instanceProcessor = instanceHandle.get();
        }
        std::unique_ptr<juce::Component> copy;
        // Mirrored parameter controls bind directly to their owning APVTS,
        // even while another routing INSTANCE is the currently open page.
        if (source == nullptr || metadata.getProperty("kind", {}).toString() == "parameter"
            || metadata.getProperty("kind", {}).toString() == "choice"
            || metadata.getProperty("kind", {}).toString() == "accessory")
        {
            if (auto* state = dockParameterState(*instanceProcessor, metadata))
            {
                const auto kind = metadata.getProperty("kind", {}).toString();
                const auto paramId = metadata.getProperty("id", {}).toString();
                auto title = metadata.getProperty("title", {}).toString();
                if (state->state.hasType("chain_state"))
                {
                    const auto suffix = paramId.fromFirstOccurrenceOf("_", false, false);
                    for (const auto& spec : crossover_configs::makeBlockRangeControls())
                        if (suffix == spec.suffix) { title = spec.label; break; }
                }
                if (kind == "parameter")
                    copy = std::make_unique<ParameterControl>(*state, paramId, title,
                        int(metadata.getProperty("decimals", 2)));
                else if (kind == "choice")
                    copy = std::make_unique<ChoiceControl>(*state, paramId, title);
                else if (kind == "accessory")
                    copy = makeDockAccessoryControl(*state, paramId, title);
                if (auto* field = dynamic_cast<ParameterField*>(copy.get()))
                {
                    field->setBottomIndicatorsVisible(bool(metadata.getProperty("bottom0", false)),
                                                      bool(metadata.getProperty("bottom1", false)));
                    field->setRangeIndicatorsVisible(bool(metadata.getProperty("range", true)));
                }
            }
        }
        else
        {
        if (auto* parameter = dynamic_cast<ParameterControl*>(source))
        {
            if (auto* state = parameter->getBoundState())
            {
                auto control = std::make_unique<ParameterControl>(*state,
                    parameter->getParameterId(), parameter->getTitleText(), parameter->getEditorDecimals());
                control->setValuePresentation(parameter->getValuePresentation());
                control->setBottomIndicatorsVisible(parameter->isBottomIndicatorVisible(0),
                                                    parameter->isBottomIndicatorVisible(1));
                control->setRangeIndicatorsVisible(parameter->areRangeIndicatorsVisible());
                copy = std::move(control);
            }
        }
        else if (auto* choice = dynamic_cast<ChoiceControl*>(source))
        {
            if (auto* state = choice->getBoundState())
            {
                auto control = std::make_unique<ChoiceControl>(*state,
                    choice->getParameterId(), choice->getTitleText());
                control->setBottomIndicatorsVisible(choice->isBottomIndicatorVisible(0),
                                                    choice->isBottomIndicatorVisible(1));
                control->setRangeIndicatorsVisible(choice->areRangeIndicatorsVisible());
                copy = std::move(control);
            }
        }
        else if (auto* accessory = dynamic_cast<AccessoryButton*>(source))
        {
            if (auto* state = accessory->getBoundState())
                copy = makeDockAccessoryControl(*state, accessory->getParameterId(),
                    accessory->getButtonText());
        }
        else if (auto* local = dynamic_cast<LocalChoiceControl*>(source))
        {
            auto control = std::make_unique<LocalChoiceControl>(local->getTitleText(),
                local->getChoiceItems(), local->getSelectedChoiceIndex());
            control->setAlwaysUseChoiceList(local->usesChoiceListWithTwoOptions());
            auto safe = juce::Component::SafePointer<juce::Component>(source);
            auto* proxy = control.get();
            control->onValueChanged = [safe, proxy]
            {
                if (auto* original = dynamic_cast<LocalChoiceControl*>(safe.getComponent()))
                    original->setSelectedChoiceIndex(proxy->getSelectedChoiceIndex(), true);
            };
            control->setBottomIndicatorsVisible(false, false);
            copy = std::move(control);
        }
        else if (auto* push = dynamic_cast<PushControl*>(source))
        {
            auto control = std::make_unique<PushControl>(push->getTitleText());
            auto safe = juce::Component::SafePointer<juce::Component>(source);
            control->onTrigger = [safe]
            {
                if (auto* original = dynamic_cast<PushControl*>(safe.getComponent()))
                    if (original->onTrigger) original->onTrigger();
            };
            control->setBottomIndicatorsVisible(false, false);
            copy = std::move(control);
        }
        else if (auto* text = dynamic_cast<TextControl*>(source))
        {
            auto control = std::make_unique<TextControl>(text->getTitleText(), text->getValue(), text->isEditable());
            auto safe = juce::Component::SafePointer<juce::Component>(source);
            control->onValueChanged = [safe](const juce::String& entered)
            {
                if (auto* original = dynamic_cast<TextControl*>(safe.getComponent()))
                    return original->setValue(entered, true);
                return false;
            };
            copy = std::move(control);
        }
        else if (auto* readout = dynamic_cast<SquareTextLabel*>(source))
        {
            auto control = std::make_unique<TextControl>(readout->getText(), readout->getText(), false);
            control->setBottomIndicatorsVisible(false, false);
            control->setRangeIndicatorsVisible(false);
            copy = std::move(control);
        }
        else if (auto* button = dynamic_cast<BoxTextButton*>(source))
        {
            auto control = std::make_unique<PushControl>(button->getButtonText());
            auto safe = juce::Component::SafePointer<juce::Component>(source);
            control->onTrigger = [safe]
            {
                if (auto* original = dynamic_cast<BoxTextButton*>(safe.getComponent()))
                    original->triggerClick();
            };
            control->setBottomIndicatorsVisible(false, false);
            copy = std::move(control);
        }
        }
        if (copy == nullptr) continue;
        std::unique_ptr<AccessoryButton> accessoryCopy;
        if (auto* mirror = dynamic_cast<ParameterField*>(copy.get()))
        {
            const auto* original = source != nullptr ? dockEmbeddedAccessory(*source) : nullptr;
            auto auxiliaryId = metadata.getProperty("auxiliaryId", {}).toString();
            auto auxiliaryTitle = metadata.getProperty("auxiliaryTitle", {}).toString();
            if (original != nullptr)
            {
                auxiliaryId = original->getParameterId();
                auxiliaryTitle = original->getButtonText();
            }
            if (auxiliaryId.isNotEmpty())
            {
                const auto auxiliaryState = original != nullptr && original->getBoundState() != nullptr
                    ? original->getBoundState()->state.getType().toString()
                    : metadata.getProperty("auxiliaryState", {}).toString();
                if (auto* state = dockParameterState(*instanceProcessor, metadata, auxiliaryId, auxiliaryState))
                    if (auto* parameter = state->getParameter(auxiliaryId))
                    {
                        accessoryCopy = std::make_unique<AccessoryButton>(auxiliaryTitle);
                        accessoryCopy->bindParameter(*state, auxiliaryId,
                            dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr);
                        // A Ctrl+click on a mirrored accessory unpins the
                        // containing tile; independently pin it from its
                        // original button to preserve the owning INSTANCE.
                        accessoryCopy->getProperties().set("independentDockSelection", false);
                        // Reuse the original action when available. This also
                        // preserves auxiliary behaviour such as GAIN's mute
                        // threshold and SOLO's manual selection path.
                        if (original != nullptr)
                        {
                            auto safe = juce::Component::SafePointer<AccessoryButton>(
                                const_cast<AccessoryButton*>(original));
                            const auto fallback = accessoryCopy->onClick;
                            accessoryCopy->onClick = [safe, fallback]
                            {
                                if (auto* sourceAccessory = safe.getComponent())
                                {
                                    if (sourceAccessory->onClick) sourceAccessory->onClick();
                                }
                                else if (fallback) fallback();
                            };
                        }
                        mirror->setBottomAccessory(accessoryCopy.get());
                    }
            }
        }
        // The lower-left dock badge identifies the instance containing this
        // source control. It replaces the SL slot-assignment action in the
        // mirrored tile only; the original control is unchanged.
        if (dynamic_cast<ParameterField*>(copy.get()) != nullptr)
        {
            copy->getProperties().set("dockInstanceNumber",
                unitWide ? juce::String("00")
                         : dockBadgeNumber(dockModuleStatus(*instanceProcessor, metadata), instanceId));
            copy->getProperties().set("dockInstanceName",
                unitWide ? juce::String("UNIT") : ava::routing::getDisplayName(topology, instanceId));
        }
        if (auto* original = dynamic_cast<ParameterField*>(source))
            if (auto* mirror = dynamic_cast<ParameterField*>(copy.get()))
                mirror->setRangeIndicatorsVisible(original->areRangeIndicatorsVisible());
        if (source != nullptr && bool(source->getProperties().getWithDefault("readOnlyControl", false)))
            copy->getProperties().set("readOnlyControl", true);
        copy->getProperties().set("quickAccessSelectionKey", prefix + id);
        quickAccessContent.addAndMakeVisible(*copy);
        quickAccessItems.push_back({ id, juce::Component::SafePointer<juce::Component>(source),
                                     std::move(instanceHandle), std::move(copy),
                                     std::move(accessoryCopy) });
    }
    layoutQuickAccessStrip();
    syncQuickAccessControls();
    repaint();
}

void AvaAudioProcessorEditor::syncQuickAccessControls()
{
    if (audioProcessor.getRoutingOwner().getValueTreeState().state.getProperty(
            shell_editor_ui::key("quick_access")).toString() != quickAccessOrderCache)
    {
        refreshQuickAccessControls();
        return;
    }
    for (auto& item : quickAccessItems)
    {
        const auto& rootState = audioProcessor.getRoutingOwner().getValueTreeState().state;
        const auto metadata = juce::JSON::parse(rootState.getProperty(dockMetadataKey(item.key)).toString());
        const auto topology = ava::routing::readState(rootState);
        const bool unitWide = isUnitWideDockMetadata(metadata);
        const auto instanceId = unitWide ? topology.rootInstanceId
            : int(metadata.getProperty("instance", topology.rootInstanceId));
        if (instanceId != topology.rootInstanceId)
        {
            const auto currentHandle = audioProcessor.getRoutingOwner().getRoutingInstanceHandle(instanceId);
            if (currentHandle != item.instanceHandle)
            {
                quickAccessOrderCache.clear();
                refreshQuickAccessControls();
                return;
            }
        }
        auto* source = item.source.getComponent();
        auto* copy = item.control.get();
        auto* processor = item.instanceHandle != nullptr ? item.instanceHandle.get()
            : &audioProcessor.getRoutingOwner();
        if (auto* param = dynamic_cast<ParameterControl*>(copy))
        {
            if (dockParameterState(*processor, metadata) != param->getBoundState())
            {
                quickAccessOrderCache.clear();
                refreshQuickAccessControls();
                return;
            }
        }
        else if (auto* choice = dynamic_cast<ChoiceControl*>(copy))
        {
            if (dockParameterState(*processor, metadata) != choice->getBoundState())
            {
                quickAccessOrderCache.clear();
                refreshQuickAccessControls();
                return;
            }
        }
        if (copy == nullptr) continue;
        const auto module = dockModuleStatus(*processor, metadata);
        const auto badge = unitWide ? juce::String("00") : dockBadgeNumber(module, instanceId);
        if (copy->getProperties()["dockInstanceNumber"].toString() != badge)
        {
            copy->getProperties().set("dockInstanceNumber", badge);
            repaintValueOverlay(*copy);
        }
        // OFF is a DSP bypass, not a removal from the dock. Both visible
        // source controls and controls of unopened routing instances use the
        // same per-band module stack state to disable their mirrored tile.
        // Use the normal control-interaction policy rather than disabling the
        // entire Component: an inactive tile still needs Ctrl+click to unpin
        // it and mouse hover to reveal its routing INSTANCE name.
        copy->setEnabled(source == nullptr || source->isEnabled());
        if (auto* parameter = dynamic_cast<ParameterControl*>(copy))
            parameter->setInteractionEnabled(module.enabled);
        else if (auto* choice = dynamic_cast<ChoiceField*>(copy))
            choice->setInteractionEnabled(module.enabled);
        else if (!module.enabled)
            copy->setEnabled(false);
        if (item.accessory != nullptr)
        {
            const auto* original = source != nullptr ? dockEmbeddedAccessory(*source) : nullptr;
            item.accessory->setEnabled(module.enabled
                && (original == nullptr || original->isEnabled()));
        }
        const auto name = unitWide ? juce::String("UNIT")
            : ava::routing::getDisplayName(topology, instanceId);
        if (copy->getProperties()["dockInstanceName"].toString() != name)
        {
            copy->getProperties().set("dockInstanceName", name);
            repaintValueOverlay(*copy);
        }
        // An unopened routing instance has no source component. Its parameter
        // mirror nevertheless stays live via the owning APVTS attachment.
        if (source == nullptr || !matchesDockSourceOwner(source, metadata,
                audioProcessor.getRoutingOwner(), routingInstanceId)) continue;
        const auto base = source->getProperties().getWithDefault("textButtonBaseColour", juce::int64(0xffbbbbbb));
        if (copy->getProperties()["textButtonBaseColour"] != base)
        {
            copy->getProperties().set("textButtonBaseColour", base);
            copy->repaint();
        }
        if (auto* original = dynamic_cast<SquareTextLabel*>(source))
            if (auto* mirror = dynamic_cast<TextControl*>(copy))
            {
                mirror->setTitleText(original->getText());
                if (mirror->getValue() != original->getText()) mirror->setValue(original->getText());
            }
        if (auto* original = dynamic_cast<BoxTextButton*>(source))
            if (auto* mirror = dynamic_cast<PushControl*>(copy))
                mirror->setTitleText(original->getButtonText());
        if (auto* original = dynamic_cast<ParameterControl*>(source))
            if (auto* mirror = dynamic_cast<ParameterControl*>(copy))
            {
                mirror->setInteractionEnabled(module.enabled && original->isInteractionEnabled());
                mirror->setTitleText(original->getTitleText());
                mirror->setValuePresentation(original->getValuePresentation());
            }
        if (auto* original = dynamic_cast<ChoiceControl*>(source))
            if (auto* mirror = dynamic_cast<ChoiceControl*>(copy))
            {
                mirror->setInteractionEnabled(module.enabled && original->isInteractionEnabled());
                mirror->setTitleText(original->getTitleText());
            }
        if (auto* original = dynamic_cast<LocalChoiceControl*>(source))
            if (auto* mirror = dynamic_cast<LocalChoiceControl*>(copy))
            {
                if (mirror->getChoiceItems() != original->getChoiceItems())
                    mirror->setChoices(original->getChoiceItems());
                mirror->setInteractionEnabled(module.enabled && original->isInteractionEnabled());
                if (mirror->getSelectedChoiceIndex() != original->getSelectedChoiceIndex())
                    mirror->setSelectedChoiceIndex(original->getSelectedChoiceIndex(), false);
                mirror->setTitleText(original->getTitleText());
            }
        if (auto* original = dynamic_cast<PushControl*>(source))
            if (auto* mirror = dynamic_cast<PushControl*>(copy))
            {
                mirror->setInteractionEnabled(module.enabled && original->isInteractionEnabled());
                mirror->setTitleText(original->getTitleText());
            }
        if (auto* original = dynamic_cast<TextControl*>(source))
            if (auto* mirror = dynamic_cast<TextControl*>(copy))
            {
                mirror->setTitleText(original->getTitleText());
                if (mirror->getValue() != original->getValue()) mirror->setValue(original->getValue());
            }
    }
}

void AvaAudioProcessorEditor::dragDockControl(const juce::MouseEvent& event, int phase)
{
    const auto count = int(quickAccessItems.size());
    const auto side = getSquareControlSize(*this);
    const auto step = side + getUiGap(*this);
    const bool vertical = isDockOnly() && bool(getRootWindowState().getProperty(shell_editor_ui::key("dock_vertical"), false));
    const auto x = juce::roundToInt(vertical ? event.position.y : event.position.x);
    if (phase == 0)
    {
        const auto contentX = x + (vertical ? quickAccessViewport.getViewPositionY() : quickAccessViewport.getViewPositionX());
        dockDragSource = contentX >= 0 && contentX % step < side
            && juce::isPositiveAndBelow(contentX / step, count) ? contentX / step : -1;
        dockDropTarget = dockDragSource;
    }
    if (!juce::isPositiveAndBelow(dockDragSource, count)) return;
    if (phase == 1)
    {
        auto scroll = vertical ? quickAccessViewport.getViewPositionY() : quickAccessViewport.getViewPositionX();
        if (x < uiGap) scroll -= uiGap;
        else if (x >= (vertical ? dockSortOverlay.getHeight() : dockSortOverlay.getWidth()) - uiGap) scroll += uiGap;
        if (vertical) quickAccessViewport.setViewPosition(0, juce::jlimit(0, juce::jmax(0, quickAccessContent.getHeight() - quickAccessViewport.getHeight()), scroll));
        else quickAccessViewport.setViewPosition(juce::jlimit(0, juce::jmax(0, quickAccessContent.getWidth() - quickAccessViewport.getWidth()), scroll), 0);
        dockDropTarget = juce::jlimit(0, count - 1, (x + (vertical ? quickAccessViewport.getViewPositionY() : quickAccessViewport.getViewPositionX())) / step);
        if (dockDragSource < dockDropTarget)
            std::rotate(quickAccessItems.begin() + dockDragSource,
                quickAccessItems.begin() + dockDragSource + 1, quickAccessItems.begin() + dockDropTarget + 1);
        else if (dockDragSource > dockDropTarget)
            std::rotate(quickAccessItems.begin() + dockDropTarget,
                quickAccessItems.begin() + dockDragSource, quickAccessItems.begin() + dockDragSource + 1);
        dockDragSource = dockDropTarget;
        layoutQuickAccessStrip();
    }
    for (int index = 0; index < count; ++index)
    {
        auto& control = *quickAccessItems[size_t(index)].control;
        control.getProperties().set("arrangementDragging", phase != 2 && index == dockDragSource);
        control.repaint();
    }
    if (phase != 2) return;
    dockDragSource = dockDropTarget = -1;
    juce::StringArray shown;
    for (const auto& item : quickAccessItems) shown.add(item.key);
    auto& dockState = audioProcessor.getRoutingOwner().getValueTreeState().state;
    const auto saved = dockState.getProperty(shell_editor_ui::key("quick_access")).toString();
    auto ordered = juce::StringArray::fromTokens(saved, "|", "");
    int next = 0;
    for (int index = 0; index < ordered.size(); ++index)
        if (shown.contains(ordered[index])) ordered.set(index, shown[next++]);
    const auto reordered = ordered.joinIntoString("|");
    if (reordered == saved) return;
    dockState.setProperty(shell_editor_ui::key("quick_access"), reordered, nullptr);
    refreshQuickAccessControls();
}

void AvaAudioProcessorEditor::applyDockWindowSize(const int width, const int height)
{
    const juce::ScopedValueSetter<bool> guard(updatingEditorResizeLimits, true);
    if (auto* frame = dynamic_cast<DockWindowResizer*>(getParentComponent()))
    {
        frame->resizeDockContent(width, height);
        return;
    }
    setResizeLimits(juce::jmin(getWidth(), width), juce::jmin(getHeight(), height),
                    juce::jmax(getWidth(), width), juce::jmax(getHeight(), height));
    setSize(width, height);
    setResizeLimits(width, getMinimumEditorHeight(), width, getMaximumEditorHeight());
}

void AvaAudioProcessorEditor::setDockOnly(bool dockOnly)
{
    auto* root = this;
    while (auto* parent = root->findParentComponentOfClass<AvaAudioProcessorEditor>()) root = parent;
    if (root->isDockOnly() == dockOnly) return;

    // The compact dock is a different window layout, not a user resize of
    // the full editor. Preserve its *actual* last expanded height exactly.
    if (dockOnly)
    {
        root->expandedHeightBeforeDock = root->getHeight();
        root->storeEditorStateToValueTree();
    }
    const auto expandedWidth = getExpandedEditorWidth(shell_editor_ui::editorColumns(root->getRootWindowState()));
    const auto savedHeight = root->expandedHeightBeforeDock > 0
        ? root->expandedHeightBeforeDock
        : int(root->getRootWindowState().getProperty(
            AvaAudioProcessor::editorHeightStateKey, expandedWidth));
    const auto expandedHeight = juce::jlimit(expandedWidth, maximumEditorHeight, savedHeight);

    dismissTextPrompt();
    if (root != this) root->dismissTextPrompt();
    {
        const juce::ScopedValueSetter<bool> saveGuard(root->suppressEditorSizeStateSave, true);
        root->getRootWindowState().setProperty(shell_editor_ui::key("dock_only"), dockOnly, nullptr);
        const auto targetWidth = getEditorWidth(*root);
        const auto targetHeight = dockOnly ? root->getMinimumEditorHeight() : expandedHeight;
        root->applyDockWindowSize(targetWidth, targetHeight);
        root->updateSectionStates();
        if (root->activeInstanceEditor != nullptr) root->activeInstanceEditor->updateSectionStates();
        root->resized();
    }
    root->storeEditorStateToValueTree();
}
