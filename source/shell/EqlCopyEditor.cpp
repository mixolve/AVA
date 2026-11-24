#include "EqlCopyEditor.h"
#include "FilterOrderState.h"
#include "Metrics.h"
#include "../modules/eql/FilterSupport.h"
#include "../modules/eql/Presets.h"
#include <algorithm>
#include <cmath>

namespace
{
using ParameterId = juce::String (*)(int);
constexpr std::array<ParameterId, 7> filterParameterIds {
    &EqlModuleProcessor::getFilterTypeParamId,
    &EqlModuleProcessor::getFilterPhaseParamId,
    &EqlModuleProcessor::getFilterFrequencyParamId,
    &EqlModuleProcessor::getFilterBandwidthParamId,
    &EqlModuleProcessor::getFilterOrderParamId,
    &EqlModuleProcessor::getFilterGainParamId,
    &EqlModuleProcessor::getFilterBypassParamId
};
constexpr auto singleFilterClipboard = "AVA-EQL-FILTER-V1\n";
constexpr auto allFiltersClipboard = "AVA-EQL-FILTERS-V1\n";

}

AvaAudioProcessorEditor::EqlCopyEditor::EqlCopyEditor(
    AvaAudioProcessorEditor& ownerIn, EqlModuleProcessor& processorIn,
    int uidIn, int bandIn)
    : owner(ownerIn), processor(processorIn), uid(uidIn), band(bandIn)
{
    getProperties().set("moduleCopyUid", uid);
    getProperties().set("moduleCopyBand", band);
    collapseBlockInitially("EQL");
    bindCollapseState(owner.valueTreeState,
        "eql.band-" + juce::String(band + 1) + ".module.uid-" + juce::String(uid));
    displayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
    restoreOrder();
    initializeControls();
    refreshPresets();
}

AvaAudioProcessorEditor::EqlCopyEditor::~EqlCopyEditor() = default;

void AvaAudioProcessorEditor::EqlCopyEditor::initializeControls()
{
    auto& state = processor.getValueTreeState();
    presets = std::make_unique<PresetsSection>();
    addAndMakeVisible(presets->presetChoice);
    for (auto* button : {presets->addButton.get(), presets->saveButton.get(),
            presets->renameButton.get(), presets->defaultButton.get(),
            presets->deleteButton.get(), presets->copyFiltersButton.get(),
            presets->pasteFilterButton.get()})
        addAndMakeVisible(*button);

    presets->onPresetSelected = [this] { loadFilterPreset(); };
    presets->addButton->onTrigger = [this] { addFilterPreset(); };
    presets->saveButton->onTrigger = [this] { saveFilterPreset(); };
    presets->renameButton->onTrigger = [this] { renameFilterPreset(); };
    presets->defaultButton->onTrigger = [this] {
        if (processor.setDefaultFilterPreset(presets->getSelectedPresetName()))
        { refreshPresets(); notifyChange(); }
    };
    presets->deleteButton->onTrigger = [this] { deleteFilterPreset(); };
    presets->copyFiltersButton->onTrigger = [this] { copyAllFilters(); };
    presets->pasteFilterButton->onTrigger = [this] { pasteAllFilters(); };

    const auto makeButton = [this](std::unique_ptr<PushControl>& target,
                                    const juce::String& label, std::function<void()> action)
    {
        target = std::make_unique<PushControl>(label);
        target->setBottomIndicatorsVisible(false, false);
        target->getProperties().set("foregroundOccluder", true);
        target->getProperties().set("disableDockSelection", true);
        target->onTrigger = std::move(action);
        addAndMakeVisible(*target);
    };
    makeButton(sortButton, "ALL\nSORT", [this] { sortFilters(); });
    makeButton(copyButton, "ONE\nCOPY", [this] { copyFilter(); });
    makeButton(pasteButton, "ONE\nPASTE", [this] { pasteFilter(); });
    makeButton(deleteButton, "ONE\nDELETE", [this] { deleteFilter(); });
    makeButton(addButton, "ONE\nADD", [this] { addFilter(); });

    for (int i = 0; i < EqlModuleProcessor::maxFilterCount; ++i)
    {
        auto section = std::make_unique<FilterSection>(state, i);
        section->header->onTrigger = [this, i] { selectFilter(i); };
        section->header->setDragAction(
            [this, i](const juce::MouseEvent& event, bool released)
            {
                if (released) {
                    const auto point = getLocalPoint(nullptr, event.getScreenPosition().toFloat());
                    int destination = -1;
                    for (int j = 0; j < processor.getActiveFilterCount(); ++j)
                        if (filters[size_t(j)] != nullptr
                            && filters[size_t(j)]->header->getBounds().toFloat().contains(point))
                        { destination = j; break; }
                    if (destination >= 0 && destination != i) {
                        const auto count = processor.getActiveFilterCount();
                        auto first = displayOrder.begin();
                        const auto from = std::find(first, first + count, i);
                        const auto to = std::find(first, first + count, destination);
                        if (from != first + count && to != first + count) {
                            const auto position = int(to - first);
                            displayOrder.erase(from);
                            displayOrder.insert(displayOrder.begin() + position, i);
                            rememberOrder();
                            owner.resized();
                        }
                    }
                }
            }, [] {});
        section->typeControl->onValueChanged = [this, i] {
            if (updating) return;
            filters[size_t(i)]->refreshTypeDependentControls();
            notifyChange();
        };
        section->orderControl->onValueChanged = [this, i] {
            if (updating) return;
            if (filters[size_t(i)]->getFilterType() == EqlModuleProcessor::FilterType::bell
                && filters[size_t(i)]->orderControl->getSelectedChoiceIndex() == 0)
            {
                const juce::ScopedValueSetter<bool> guard(updating, true);
                filters[size_t(i)]->orderControl->setSelectedChoiceIndex(
                    EqlModuleProcessor::getOrderChoiceForSlopeDbPerOct(EqlModuleProcessor::fixedSlopeDbPerOct), true);
            }
            notifyChange();
        };
        for (auto* control : std::array<ParameterControl*, 3> {
                section->frequencyControl.get(), section->bandwidthControl.get(), section->gainControl.get()})
            control->onValueChanged = [this] { if (!updating) owner.scheduleHistorySnapshot(); };
        section->frequencyControl->onResetRequested = [this, i] {
            filters[size_t(i)]->frequencyControl->setValue(defaultFilterFrequencyHz, true);
        };
        section->bandwidthControl->onResetRequested = [this, i] {
            filters[size_t(i)]->bandwidthControl->setValue(defaultFilterBandwidthOctaves, true);
        };
        section->gainControl->onResetRequested = [this, i] {
            filters[size_t(i)]->gainControl->setValue(0.0, true);
        };
        section->orderControl->onResetRequested = [this, i] {
            filters[size_t(i)]->orderControl->setSelectedChoiceIndex(
                EqlModuleProcessor::getOrderChoiceForSlopeDbPerOct(defaultFilterSlopeDbPerOct), true);
        };
        addAndMakeVisible(*section->header);
        for (auto* control : std::array<juce::Component*, 5> {
                 section->typeControl.get(), section->orderControl.get(),
                 section->frequencyControl.get(), section->bandwidthControl.get(), section->gainControl.get()})
        {
            control->getProperties().set("foregroundOccluder", true);
            addAndMakeVisible(*control);
        }
        filters[size_t(i)] = std::move(section);
    }
    lastActiveCount = processor.getActiveFilterCount();
    currentFilter = juce::jlimit(0, juce::jmax(0, lastActiveCount - 1), currentFilter);
}

void AvaAudioProcessorEditor::EqlCopyEditor::notifyChange()
{
    owner.scheduleHistorySnapshot();
    owner.audioProcessor.notifyHostOfStateChange();
    owner.resized();
}

void AvaAudioProcessorEditor::EqlCopyEditor::refreshFilterControls()
{
    const juce::ScopedValueSetter<bool> guard(updating, true);
    const int count = processor.getActiveFilterCount();
    for (int i = 0; i < EqlModuleProcessor::maxFilterCount; ++i)
    {
        auto* f = filters[size_t(i)].get();
        if (f == nullptr) continue;
        const auto type = f->getFilterType();
        f->refreshTypeDependentControls(false);
        f->updatePhaseForType(false);
        const auto availability = getFilterControlAvailability(type, f->orderControl->getSelectedChoiceIndex());
        f->orderControl->setInteractionEnabled(availability.order);
        if (availability.order) f->orderControl->clearOverrideText();
        else f->orderControl->setOverrideText("OFF");
        f->updateFrequencyRangeForType();
        f->frequencyControl->setInteractionEnabled(availability.frequency);
        if (availability.frequency) f->frequencyControl->clearOverrideText();
        else f->frequencyControl->setOverrideText("OFF");
        f->bandwidthControl->setInteractionEnabled(availability.bandwidth);
        if (availability.bandwidth) f->bandwidthControl->clearOverrideText();
        else f->bandwidthControl->setOverrideText("OFF");
        f->gainControl->setInteractionEnabled(availability.gain);
        if (availability.gain) f->gainControl->clearOverrideText();
        else f->gainControl->setOverrideText("OFF");
        f->setGainDisplaysDegrees(f->isPhaseEnabled() && availability.gain);
    }
    deleteButton->setInteractionEnabled(count > 1);
    sortButton->setInteractionEnabled(count > 1);
    if (lastActiveCount != count)
    {
        lastActiveCount = count;
        if (currentFilter >= count) currentFilter = juce::jmax(0, count - 1);
        if (!shell_filter_order_state::decode(shell_filter_order_state::encode(displayOrder, count),
                                               count, EqlModuleProcessor::maxFilterCount))
            displayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
    }
}

void AvaAudioProcessorEditor::EqlCopyEditor::updateLayout(int width)
{
    currentWidth = width;
    const auto gap = getUiGap(*this);
    const auto inset = controlBlockPadding + controlBlockFrameThickness;
    const auto frameWidth = juce::jmax(0, width - gap);
    const auto collapsed = isBlockCollapsed("EQL");
    refreshFilterControls();
    for (auto* button : {sortButton.get(), copyButton.get(), pasteButton.get(), deleteButton.get(), addButton.get()})
        button->setVisible(!collapsed);
    for (auto* control : std::array<juce::Component*, 8> {
            &presets->presetChoice, presets->addButton.get(), presets->saveButton.get(),
            presets->renameButton.get(), presets->defaultButton.get(), presets->deleteButton.get(),
            presets->copyFiltersButton.get(), presets->pasteFilterButton.get()})
        control->setVisible(!collapsed);

    std::array<std::vector<ControlGridCell>, 4> rows;
    if (!collapsed)
    {
        rows[0] = {{&presets->presetChoice}, {presets->addButton.get()},
            {presets->saveButton.get()}, {presets->renameButton.get()},
            {presets->defaultButton.get()}, {presets->deleteButton.get()}};
        rows[1] = {{presets->copyFiltersButton.get()},
            {presets->pasteFilterButton.get()}, {sortButton.get()}};
        auto* selected = filters[size_t(currentFilter)].get();
        if (selected != nullptr)
            rows[2] = {{selected->typeControl.get()}, {selected->orderControl.get()},
                {selected->frequencyControl.get()}, {selected->bandwidthControl.get()},
                {selected->gainControl.get()}};
        rows[3] = {{copyButton.get()}, {pasteButton.get()},
            {deleteButton.get()}, {addButton.get()}};
    }
    const int count = processor.getActiveFilterCount();
    for (int position = 0; position < count; ++position)
    {
        const int i = displayOrder[size_t(position)];
        auto* f = filters[size_t(i)].get();
        if (!f) continue;
        f->header->setVisible(!collapsed);
        f->header->setTitleText(juce::String(position + 1) + " " + processor.getFilterHeaderText(i));
        const bool selected = i == currentFilter;
        f->header->setSelectionHighlighted(selected);
        for (auto* control : std::array<juce::Component*, 5> {
                f->typeControl.get(), f->orderControl.get(), f->frequencyControl.get(),
                f->bandwidthControl.get(), f->gainControl.get()})
            control->setVisible(!collapsed && selected);
        if (!collapsed) rows[3].push_back({f->header.get()});
    }
    for (int i = count; i < EqlModuleProcessor::maxFilterCount; ++i)
    {
        auto* f = filters[size_t(i)].get();
        if (!f) continue;
        f->header->setVisible(false);
        for (auto* control : std::array<juce::Component*, 5> {
                f->typeControl.get(), f->orderControl.get(), f->frequencyControl.get(),
                f->bandwidthControl.get(), f->gainControl.get()})
            control->setVisible(false);
    }
    int height = controlBlockHeaderHeight;
    if (!collapsed)
    {
        const juce::Rectangle<int> grid {inset, controlBlockHeaderHeight + gap,
            juce::jmax(1, frameWidth - 2 * inset), 0};
        auto rowTop = grid.getY();
        auto contentBottom = rowTop;
        for (const auto& row : rows)
        {
            if (row.empty()) continue;
            const auto placed = layoutControlGrid(*this, grid.withY(rowTop), row);
            contentBottom = placed.getBottom();
            rowTop = contentBottom + getUiRowGap(*this);
        }
        height = contentBottom + gap + controlBlockFrameThickness;
    }
    setSize(width, height);
    controlBlockFrames = {{{0, 0, frameWidth, height}, "EQL", uiGreyDark,
        "EQL (" + juce::String(ava::module_stack::displayOrdinal(owner.audioProcessor.getModuleStackForRange(size_t(band)), uid)).paddedLeft('0', 2) + ")"}};
    repaint();
}

void AvaAudioProcessorEditor::EqlCopyEditor::refreshFromProcessor()
{
    if (lastActiveCount != processor.getActiveFilterCount()) owner.resized();
}

void AvaAudioProcessorEditor::EqlCopyEditor::selectFilter(int filterIndex)
{
    if (!juce::isPositiveAndBelow(filterIndex, processor.getActiveFilterCount())) return;
    currentFilter = filterIndex;
    owner.resized();
}

void AvaAudioProcessorEditor::EqlCopyEditor::addFilter()
{
    if (!processor.addFilter()) return;
    displayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
    currentFilter = processor.getActiveFilterCount() - 1;
    rememberOrder();
    notifyChange();
}

void AvaAudioProcessorEditor::EqlCopyEditor::deleteFilter()
{
    if (processor.getActiveFilterCount() <= 1) return;
    const auto removed = processor.removeFilter(currentFilter);
    if (!removed) return;
    displayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
    currentFilter = juce::jmin(currentFilter, processor.getActiveFilterCount() - 1);
    rememberOrder();
    notifyChange();
}

void AvaAudioProcessorEditor::EqlCopyEditor::sortFilters()
{
    const int count = processor.getActiveFilterCount();
    std::vector<int> indices;
    for (int i = 0; i < count; ++i) indices.push_back(i);
    std::stable_sort(indices.begin(), indices.end(), [this](int a, int b) {
        auto* left = filters[size_t(a)].get();
        auto* right = filters[size_t(b)].get();
        const bool lPhase = left->isPhaseEnabled();
        const bool rPhase = right->isPhaseEnabled();
        if (lPhase != rPhase) return !lPhase;
        const double lf = left->getFilterType() == EqlModuleProcessor::FilterType::volume ? 0.0 : left->getFrequency();
        const double rf = right->getFilterType() == EqlModuleProcessor::FilterType::volume ? 0.0 : right->getFrequency();
        return lf < rf;
    });
    std::copy(indices.begin(), indices.end(), displayOrder.begin());
    rememberOrder();
    owner.resized();
}

void AvaAudioProcessorEditor::EqlCopyEditor::rememberOrder()
{
    const auto count = processor.getActiveFilterCount();
    const auto encoded = shell_filter_order_state::encode(displayOrder, count);
    if (encoded.isNotEmpty())
        owner.valueTreeState.state.setProperty(
            juce::Identifier("ava.editor.ui.eql.copy.order.band-" + juce::String(band + 1)
                + ".uid-" + juce::String(uid)), encoded, nullptr);
}

void AvaAudioProcessorEditor::EqlCopyEditor::restoreOrder()
{
    const auto count = processor.getActiveFilterCount();
    const auto encoded = owner.valueTreeState.state.getProperty(
        juce::Identifier("ava.editor.ui.eql.copy.order.band-" + juce::String(band + 1)
            + ".uid-" + juce::String(uid))).toString();
    if (const auto decoded = shell_filter_order_state::decode(encoded, count, EqlModuleProcessor::maxFilterCount))
        displayOrder = *decoded;
}

void AvaAudioProcessorEditor::EqlCopyEditor::copyFilter()
{
    auto& state = processor.getValueTreeState();
    juce::Array<juce::var> values;
    for (auto id : filterParameterIds)
    {
        auto* parameter = state.getParameter(id(currentFilter));
        if (parameter == nullptr) return;
        values.add(double(parameter->convertFrom0to1(parameter->getValue())));
    }
    juce::SystemClipboard::copyTextToClipboard(juce::String(singleFilterClipboard)
        + juce::JSON::toString(juce::var(values), true));
}

void AvaAudioProcessorEditor::EqlCopyEditor::pasteFilter()
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    if (!text.startsWith(singleFilterClipboard)) return;
    const auto parsed = juce::JSON::parse(text.substring(juce::String(singleFilterClipboard).length()));
    const auto* values = parsed.getArray();
    if (values == nullptr || values->size() != int(filterParameterIds.size())) return;
    auto& state = processor.getValueTreeState();
    std::array<juce::RangedAudioParameter*, filterParameterIds.size()> params {};
    std::array<float, filterParameterIds.size()> normalized {};
    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto& v = values->getReference(int(i));
        if (!(v.isInt() || v.isInt64() || v.isDouble())) return;
        params[i] = state.getParameter(filterParameterIds[i](currentFilter));
        if (params[i] == nullptr || !std::isfinite(double(v))) return;
        normalized[i] = params[i]->convertTo0to1(float(double(v)));
        if (!std::isfinite(normalized[i])) return;
    }
    const juce::ScopedValueSetter<bool> guard(updating, true);
    for (size_t i = 0; i < params.size(); ++i)
    {
        params[i]->beginChangeGesture();
        params[i]->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalized[i]));
        params[i]->endChangeGesture();
    }
    filters[size_t(currentFilter)]->refreshTypeDependentControls(false);
    notifyChange();
}

void AvaAudioProcessorEditor::EqlCopyEditor::copyAllFilters()
{
    const int count = processor.getActiveFilterCount();
    if (count < 1) return;
    const auto order = shell_filter_order_state::encode(displayOrder, count);
    if (order.isEmpty()) return;
    juce::Array<juce::var> rows;
    auto& state = processor.getValueTreeState();
    for (int f = 0; f < count; ++f)
    {
        juce::Array<juce::var> values;
        for (auto id : filterParameterIds)
        {
            auto* parameter = state.getParameter(id(f));
            if (!parameter) return;
            values.add(double(parameter->convertFrom0to1(parameter->getValue())));
        }
        rows.add(juce::var(values));
    }
    juce::Array<juce::var> data;
    data.add(count);
    data.add(juce::var(rows));
    data.add(order);
    juce::SystemClipboard::copyTextToClipboard(juce::String(allFiltersClipboard)
        + juce::JSON::toString(juce::var(data), true));
}

void AvaAudioProcessorEditor::EqlCopyEditor::pasteAllFilters()
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    if (!text.startsWith(allFiltersClipboard)) return;
    const auto parsed = juce::JSON::parse(text.substring(juce::String(allFiltersClipboard).length()));
    const auto* data = parsed.getArray();
    if (!data || data->size() != 3 || !data->getReference(0).isInt()) return;
    const int count = int(data->getReference(0));
    if (count < 1 || count > EqlModuleProcessor::maxFilterCount) return;
    const auto* rows = data->getReference(1).getArray();
    if (!rows || rows->size() != count) return;
    const auto order = shell_filter_order_state::decode(data->getReference(2).toString(),
        count, EqlModuleProcessor::maxFilterCount);
    if (!order) return;
    auto& state = processor.getValueTreeState();
    std::vector<std::array<float, filterParameterIds.size()>> values(static_cast<size_t>(count));
    for (int f = 0; f < count; ++f)
    {
        const auto* row = rows->getReference(f).getArray();
        if (!row || row->size() != int(filterParameterIds.size())) return;
        for (size_t i = 0; i < filterParameterIds.size(); ++i)
        {
            const auto& v = row->getReference(int(i));
            if (!(v.isInt() || v.isInt64() || v.isDouble()) || !std::isfinite(double(v))) return;
            auto* parameter = state.getParameter(filterParameterIds[i](f));
            if (!parameter) return;
            values[size_t(f)][i] = parameter->convertTo0to1(float(double(v)));
            if (!std::isfinite(values[size_t(f)][i])) return;
        }
    }
    const juce::ScopedValueSetter<bool> guard(updating, true);
    while (processor.getActiveFilterCount() > count)
        if (!processor.removeFilter(processor.getActiveFilterCount() - 1)) return;
    while (processor.getActiveFilterCount() < count)
        if (!processor.addFilter()) return;
    for (int f = 0; f < count; ++f)
        for (size_t i = 0; i < filterParameterIds.size(); ++i)
        {
            auto* parameter = state.getParameter(filterParameterIds[i](f));
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, values[size_t(f)][i]));
            parameter->endChangeGesture();
        }
    displayOrder = *order;
    currentFilter = displayOrder[0];
    rememberOrder();
    notifyChange();
}

void AvaAudioProcessorEditor::EqlCopyEditor::refreshPresets()
{
    const auto names = processor.getFilterPresetNames();
    const auto selected = processor.getSelectedFilterPresetName();
    presets->setPresetNames(names, selected);
    const auto hasSelected = presets->getSelectedPresetName().isNotEmpty();
    const auto isNone = presets->getSelectedPresetName().equalsIgnoreCase(eql_presets::nonePresetName);
    presets->renameButton->setInteractionEnabled(hasSelected && !isNone);
    presets->deleteButton->setInteractionEnabled(names.size() > 1 && hasSelected && !isNone);
    presets->defaultButton->setInteractionEnabled(hasSelected);
    presets->defaultButton->setRestingState(hasSelected
        && selected.equalsIgnoreCase(processor.getDefaultFilterPresetName()));
}

void AvaAudioProcessorEditor::EqlCopyEditor::loadFilterPreset()
{
    if (processor.loadFilterPreset(presets->getSelectedPresetName()))
    {
        displayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
        currentFilter = 0;
        for (auto& f : filters) if (f) f->refreshTypeDependentControls(false);
        rememberOrder();
        refreshPresets();
        notifyChange();
    }
}

void AvaAudioProcessorEditor::EqlCopyEditor::saveFilterPreset()
{
    const auto name = presets->getSelectedPresetName();
    if (name.isEmpty()) return;
    const auto count = processor.getActiveFilterCount();
    std::vector<int> order(displayOrder.begin(), displayOrder.begin() + count);
    processor.applyFilterOrder(order);
    displayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
    if (processor.saveFilterPreset(name)) { refreshPresets(); rememberOrder(); notifyChange(); }
}

void AvaAudioProcessorEditor::EqlCopyEditor::addFilterPreset()
{
    auto name = presets->getSelectedPresetName();
    if (name.isEmpty() || name.equalsIgnoreCase(eql_presets::nonePresetName)) name = "PRESET";
    const auto names = processor.getFilterPresetNames();
    auto candidate = name;
    for (int number = 2; names.contains(candidate, true); ++number)
        candidate = name + " " + juce::String(number);
    const int count = processor.getActiveFilterCount();
    std::vector<int> order(displayOrder.begin(), displayOrder.begin() + count);
    processor.applyFilterOrder(order);
    displayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
    if (processor.saveFilterPreset(candidate)) { refreshPresets(); rememberOrder(); notifyChange(); }
}

void AvaAudioProcessorEditor::EqlCopyEditor::renameFilterPreset()
{
    const auto oldName = presets->getSelectedPresetName();
    if (oldName.isEmpty()) return;
    owner.showTextPrompt(oldName, [safe = juce::Component::SafePointer<EqlCopyEditor>(this), oldName]
        (const juce::String& newName) -> bool
        {
            if (safe == nullptr || !safe->processor.renameFilterPreset(oldName, newName)) return false;
            safe->refreshPresets();
            safe->notifyChange();
            return true;
        }, owner.getLocalArea(&presets->presetChoice, presets->presetChoice.getLocalBounds()),
        {}, {}, true, nullptr, {}, {}, {}, &presets->presetChoice);
}

void AvaAudioProcessorEditor::EqlCopyEditor::deleteFilterPreset()
{
    if (processor.deleteFilterPreset(presets->getSelectedPresetName()))
    { refreshPresets(); notifyChange(); }
}
