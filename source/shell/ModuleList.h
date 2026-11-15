#pragma once

#include "Editor.h"
#include "ModuleOrder.h"
#include "Controls.h"
#include "ParameterControl.h"
#include "ValueEditor.h"
#include "../modules/shared/BlockCatalog.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <map>
#include <utility>
#include <vector>

struct ModuleListItem {
    int id;
    juce::String name;
    int serial = 1;
    bool enabled = true;
    bool canDelete = false;
};

// Inline MODULES grid: click toggles a module; dragging rearranges its position.
class ModuleList final : public juce::Component
{
public:
    using Order = std::vector<int>;
    using Id = int;
    using Change = std::function<void(const Order&)>;
    ModuleList(Order initial, Change commitIn,
                        std::vector<ModuleListItem> items={})
        : order(initial), committed(initial), commit(std::move(commitIn))
    {
        setName("MODULES");
        getProperties().set("disableDockSelection", true);
        // The standard ValueBox/RangeIndicator renderer paints the module controls.
        setWantsKeyboardFocus(true);
        viewport.setViewedComponent(&list, false);
        // Clip controls to the scrolling area.
        viewport.getProperties().set("controlViewportClip", true);
        viewport.setScrollBarsShown(false, false);
        viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        list.setInterceptsMouseClicks(false,true);
        panel.setOpaque(false);
        panel.addAndMakeVisible(viewport);
        addAndMakeVisible(panel);
        createRows(items);

    }
    ~ModuleList() override {
        if (dragging) juce::Component::beginDragAutoRepeat(0);
    }

    // The arrangement is a real list of processor instances, not a permutation
    // of a fixed set of module types. Duplicate UIDs remain stable across reordering.
    void createRows(const std::vector<ModuleListItem>& items)
    {
        rows.clear();
        for (const auto& item : items)
        {
            auto row = std::make_unique<Row>(*this, item);
            list.addAndMakeVisible(*row);
            rows.emplace(item.id,std::move(row));
        }
        resized();
    }
    void setInstanceActions(std::function<int(int)> addIn,
                            std::function<bool(int)> delIn,
                            std::function<void(int)> toggleIn,
                            std::function<void(int)> selectIn,
                            std::function<std::vector<ModuleListItem>()> sourceIn)
    {
        addAction=std::move(addIn);deleteAction=std::move(delIn);
        toggleAction=std::move(toggleIn);selectAction=std::move(selectIn);
        reloadItems=std::move(sourceIn);
    }
    void doAdd(int uid) {
        if (!addAction) return;
        const int createdUid = addAction(uid);
        if (createdUid >= 0) {
            queueReload();
        } else {
            // A rejected ADD must never look like a click that did nothing.
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "AVA", "ADD could not create a module instance.");
        }
    }
    void doDelete(int uid) {
        if (deleteAction && deleteAction(uid)) queueReload();
    }
    void doToggle(int uid) { if(toggleAction) toggleAction(uid); queueReload(); }
    void doSelect(int uid) { if(selectAction) selectAction(uid); }
    void refreshItemsIfChanged()
    {
        if (dragging || reloadQueued || !reloadItems) return;
        const auto items = reloadItems();
        if (items.size() != order.size()) { queueReload(); return; }
        for (size_t index = 0; index < items.size(); ++index)
        {
            const auto it = rows.find(items[index].id);
            if (it == rows.end() || order[index] != items[index].id
                || it->second->serial != items[index].serial
                || it->second->localEnabled != items[index].enabled
                || it->second->canDelete != items[index].canDelete)
            {
                queueReload();
                return;
            }
        }
    }
    void queueReload()
    {
        if (reloadQueued) return;
        reloadQueued = true;
        juce::MessageManager::callAsync([safe=juce::Component::SafePointer<ModuleList>(this)] {
            if (safe == nullptr) return;
            safe->reloadQueued = false;
            if (!safe->reloadItems) return;
            const auto items=safe->reloadItems();
            safe->order.clear();
            for(const auto& item:items)safe->order.push_back(item.id);
            safe->committed=safe->order;
            safe->createRows(items);
            if (safe->layoutChanged)
                safe->layoutChanged();
        });
    }

    void setLayoutChanged(std::function<void()> callback)
    {
        layoutChanged = std::move(callback);
    }
    int getContentHeight(int width) const noexcept
    {
        const int size = getSquareControlSize(*this), gap = getUiGap(*this);
        const int columnsForWidth = juce::jmax(1,
            (width - 2 * (controlBlockPadding + controlBlockFrameThickness) + gap) / (size + gap));
        const int rowCount = (int(order.size()) + columnsForWidth - 1) / columnsForWidth;
        if (rowCount == 0) return 0;
        return gap + rowCount * size + (rowCount - 1) * getUiRowGap(*this)
            + gap + controlBlockFrameThickness;
    }
    void resized() override
    {
        const auto gap = getUiGap(*this), size = getSquareControlSize(*this);
        const auto width = getWidth();
        if (width <= 0) return;
        panel.setBounds(getLocalBounds());
        viewport.setBounds(panel.getLocalBounds());
        const auto padding = controlBlockPadding + controlBlockFrameThickness;
        columns = juce::jmax(1, (width - 2 * padding + gap) / (size + gap));
        contentTopInset = gap;
        list.setSize(width, juce::jmax(getHeight(), getContentHeight(width)));
        layoutRows();
        viewport.setViewPosition(0, 0);
    }
    void forwardWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        // Scrolling within the inline MODULES block scrolls ALL, not a hidden
        // second viewport whose content is already fully expanded.
        for (auto* parent = getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
            if (auto* outer = dynamic_cast<UiScrollViewport*>(parent))
            {
                outer->mouseWheelMove(event.getEventRelativeTo(outer), wheel);
                return;
            }
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        forwardWheel(event, wheel);
    }

private:
    juce::Component panel;
    struct Row final : juce::Component {
        Row(ModuleList& ownerIn, const ModuleListItem& item)
            : owner(ownerIn), id(item.id), text(item.name), control(juce::String()),
              serial(item.serial), localEnabled(item.enabled), canDelete(item.canDelete)
        {
            setName(text);
            // Reuse the standard ValueBoxComponent and embedded actions for
            // appearance; route their presses explicitly because this row
            // also supports selecting and dragging a module.
            control.setBottomIndicatorsVisible(false, false);
            // Stable identity for the module row.
            control.setComponentID("module-slot-" + juce::String(id));
            control.onTrigger = [this] {
                const auto point = control.getMouseXYRelative();
                if (control.getBottomTextZone(control.getLocalBounds(), 0, 2).contains(point)
                    || control.getBottomTextZone(control.getLocalBounds(), 1, 2).contains(point))
                    return;
                // The entire module face toggles only this instance.
                owner.doToggle(id);
            };
            control.setDragAction([this](const juce::MouseEvent& event, bool released) {
                if (pressedAction >= 0) return;
                if (released) {
                    if (owner.dragging) owner.drop();
                    return;
                }
                if (!owner.dragging) {
                    owner.dragging = true;
                    owner.dragged = id;
                    owner.setDragCursor(true);
                }
                owner.drag(event);
            }, [this] {
                if (owner.dragging) owner.drop();
            });
            // Retain the ordinary ParameterField bottom-action components,
            // including their exact paint, hover and disabled states.  The
            // MODULES row is also a drag target, so route its bottom-strip
            // clicks explicitly instead of allowing the ValueBox to treat
            // them as selecting or dragging the whole module.
            control.setBottomActions({
                { "ADD", {}, {} },
                { "DEL", {}, {} }
            });
            control.setBottomActionEnabled("DEL", canDelete);
            addAndMakeVisible(control);
            control.addMouseListener(this, true);
            control.setRestingState(isModuleEnabled());
            updateTitle();
        }
        ~Row() override { control.removeMouseListener(this); }
        bool isModuleEnabled() const noexcept { return localEnabled; }
        void updateTitle() {
            control.setTitleText(juce::String(ordinal).paddedLeft('0', 2)
                + " (" + juce::String(serial).paddedLeft('0', 2) + ")\n" + text);
        }
        // Events from BOTH the parent ValueBox and the embedded JUCE buttons
        // reach this listener.  This is required because the row uses the
        // same ValueBox for module selection and drag/reorder gestures.
        int actionAt(const juce::MouseEvent& event)
        {
            const auto point = event.getEventRelativeTo(&control).getPosition();
            for (int action = 0; action < 2; ++action)
                if (control.getBottomTextZone(control.getLocalBounds(), action, 2).contains(point))
                    return action;
            return -1;
        }
        void mouseDown(const juce::MouseEvent& event) override
        {
            // Ctrl+click marks the tile; it must not activate ADD/DEL.
            pressedAction = event.mods.isLeftButtonDown()
                && !event.mods.isCtrlDown()
                ? actionAt(event) : -1;
        }
        void mouseUp(const juce::MouseEvent& event) override
        {
            const int action = std::exchange(pressedAction, -1);
            if (owner.dragging || event.mods.isCtrlDown()
                || action < 0 || actionAt(event) != action)
                return;
            if (action == 0) owner.doAdd(id);
            else if (canDelete) owner.doDelete(id);
        }
        void resized() override {
            control.setBounds(getLocalBounds());
        }
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
            owner.forwardWheel(event, wheel);
        }
        ModuleList& owner;
        Id id;
        juce::String text;
        PushControl control;
        int serial=1;
        bool localEnabled=true;
        bool canDelete=false;
        int pressedAction=-1;
        int ordinal = 1;
    };
    int rowLeft(int row) const {
        return rows.at(order[size_t(row * columns)])->getX();
    }
    void layoutRows() {
        const auto padding = controlBlockPadding + controlBlockFrameThickness;
        std::vector<ControlGridCell> cells;
        cells.reserve(order.size());
        for(size_t i=0;i<order.size();++i) {
            auto& row = *rows.at(order[i]);
            row.ordinal = int(i) + 1;
            row.updateTitle();
            cells.push_back({&row});
            row.repaint();
        }
        layoutControlGrid(*this, {padding, contentTopInset, list.getWidth() - 2 * padding,
                                  list.getHeight() - contentTopInset}, cells);
        list.repaint();
    }
    void drag(const juce::MouseEvent& event) {
        if(!dragging || order.empty()) return;
        const auto local = event.getEventRelativeTo(&viewport).getPosition();
        viewport.autoScroll(local.x, local.y, getUiGap(*this) * 2, getUiGap(*this));
        const auto position = event.getEventRelativeTo(&list).getPosition();
        const auto gap = getUiGap(*this), step = getSquareControlSize(*this) + gap;
        const auto row = juce::jlimit(0, (int(order.size()) - 1) / columns, (position.y - contentTopInset) / (getSquareControlSize(*this) + getUiRowGap(*this)));
        const auto column = juce::jlimit(0, columns - 1, (position.x - rowLeft(row)) / step);
        const auto target = size_t(juce::jlimit(0, int(order.size()) - 1, row * columns + column));
        auto from=size_t(std::find(order.begin(),order.end(),dragged)-order.begin());
        if(from==target) return;
        const auto id=order[from];
        if(from<target) std::move(order.begin()+std::ptrdiff_t(from+1),order.begin()+std::ptrdiff_t(target+1),order.begin()+std::ptrdiff_t(from));
        else std::move_backward(order.begin()+std::ptrdiff_t(target),order.begin()+std::ptrdiff_t(from),order.begin()+std::ptrdiff_t(from+1));
        order[target]=id;
        layoutRows();
    }
    void setDragCursor(bool active) {
        // All rows use the same cursor while dragging, including when the pointer crosses a row.
        const auto cursor=active?juce::MouseCursor::DraggingHandCursor:juce::MouseCursor::NormalCursor;
        setMouseCursor(cursor);
        for(auto& [uid,row]:rows) { juce::ignoreUnused(uid); row->setMouseCursor(cursor); }
    }
    void drop() {
        if(!dragging || order.empty()) return;
        dragging=false;
        juce::Component::beginDragAutoRepeat(0);
        setDragCursor(false);
        if(order!=committed) {
            committed=order;
            if(commit) commit(order);
        }
        for (auto& [uid,row] : rows) { juce::ignoreUnused(uid); row->repaint(); }
        list.repaint();
    }
    juce::Component list;
    struct RowViewport final : juce::Viewport {
        explicit RowViewport(ModuleList& ownerIn) : owner(ownerIn) {}
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
            owner.forwardWheel(event, wheel);
        }
        ModuleList& owner;
    } viewport { *this };
    int columns = 1, contentTopInset = 0;
    std::map<Id,std::unique_ptr<Row>> rows;
    Order order,committed;
    Id dragged=Id(0);
    Change commit;
    std::function<int(int)> addAction;
    std::function<bool(int)> deleteAction;
    std::function<void(int)> toggleAction,selectAction;
    std::function<std::vector<ModuleListItem>()> reloadItems;
    bool dragging = false;
    bool reloadQueued = false;
    std::function<void()> layoutChanged;
};



