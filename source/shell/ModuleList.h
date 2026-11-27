#pragma once

#include "Editor.h"
#include "EditorStateSchema.h"
#include "PinnedRoutingOrder.h"

struct ModuleListItem
{
    int id;
    juce::String name;
    int serial;
    bool operator==(const ModuleListItem&) const = default;
};

class ModuleList final : public juce::Component
{
public:
    using Order = std::vector<int>;

    struct Actions
    {
        std::function<std::vector<ModuleListItem>()> readChain;
        std::function<void(const Order&)> reorder;
        std::function<void(int, size_t)> insert;
        std::function<void(int, bool permanently)> remove;
    };

    ModuleList(Actions actionsIn, juce::AudioProcessorValueTreeState& state)
        : actions(std::move(actionsIn)), viewState(state.state),
          catalogOrder(shell_editor_ui::inactiveModulesOrder(viewState))
    {
        getProperties().set("disableDockSelection", true);
        addAndMakeVisible(inactiveBlock);
        catalogRow.attach("modules-inactive");
        catalogRow.setScrollLabel("MODULES\nSCROLL");
        addAndMakeVisible(activeBlock);
        chainRow.attach("modules-active");
        chainRow.setScrollLabel("MODACT\nSCROLL");
        for (const auto& type : module_order::descriptions)
            catalogTiles[size_t(type.id)] = makeTile(type.id, type.name, true);
        refreshItemsIfChanged();
    }

    ~ModuleList() override
    {
        if (dragging) juce::Component::beginDragAutoRepeat(0);
    }

    void refreshItemsIfChanged(bool force = false)
    {
        if (dragging) return;
        const auto savedCatalogOrder = shell_editor_ui::inactiveModulesOrder(viewState);
        if (savedCatalogOrder != catalogOrder)
        {
            catalogOrder = savedCatalogOrder;
            layoutCatalog();
        }
        auto items = actions.readChain();
        if (!force && items == chainItems) return;
        chainItems = std::move(items);
        chainTiles.clear();
        order.clear();
        for (const auto& item : chainItems)
        {
            order.push_back(item.id);
            auto tile = makeTile(item.id, item.name, false);
            chainTiles.push_back(std::move(tile));
        }
        layoutChain();
    }

    static int getContentHeight(const juce::Component& layout)
    {
        return 2 * getSquareControlSize(layout) + getUiGap(layout);
    }

    void resized() override
    {
        layoutCatalog();
        layoutChain();
    }

private:
    std::unique_ptr<BoxTextButton> makeTile(int id, const juce::String& name, bool fromCatalog)
    {
        auto tile = std::make_unique<BoxTextButton>();
        tile->setButtonText(name);
        tile->setTextJustification(juce::Justification::centred);
        tile->setDragRequiresControl(false);
        if (fromCatalog) tile->setClickOnRelease(BoxTextButton::ClickOnRelease::none);
        tile->setComponentID(juce::String(fromCatalog ? "module-type-" : "module-slot-") + juce::String(id));
        if (!fromCatalog)
        {
            tile->getProperties().set("preserveCtrlClick", true);
            tile->onModifiedClick = [this, id](juce::ModifierKeys modifiers)
            {
                if (!modifiers.isCtrlDown() || modifiers.isShiftDown() || modifiers.isAltDown()
                    || modifiers.isCommandDown()) return;
                juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ModuleList>(this), id]
                {
                    if (safe == nullptr) return;
                    const auto remove = safe->actions.remove;
                    remove(id, true);
                    if (safe != nullptr) safe->refreshItemsIfChanged();
                });
            };
        }
        tile->onControlDragStarted = [this, button = tile.get()]
        {
            dragging = true;
            button->getProperties().set("routing.instance.dragging", true);
            button->repaint();
            juce::Component::beginDragAutoRepeat(40);
        };
        tile->onControlDrag = [this, id, fromCatalog, button = tile.get()](const juce::MouseEvent& event, bool released)
        {
            handleDrag(id, fromCatalog, *button, event, released);
        };
        return tile;
    }

    void layoutCatalog()
    {
        std::array<juce::Component*, module_order::descriptions.size()> tiles {};
        for (size_t index = 0; index < catalogOrder.size(); ++index)
            tiles[index] = catalogTiles[size_t(catalogOrder[index])].get();
        inactiveBlock.setBounds(0, 0, getWidth(), getSquareControlSize(*this));
        catalogRow.layoutInline(inactiveBlock.getLocalBounds(), tiles);
    }

    void layoutChain()
    {
        std::vector<juce::Component*> tiles;
        for (size_t index = 0; index < order.size(); ++index)
        {
            const auto item = std::find_if(chainItems.begin(), chainItems.end(), [uid = order[index]](const auto& entry)
            {
                return entry.id == uid;
            });
            auto& tile = *chainTiles[index];
            tile.setButtonText(item->serial == 0 ? item->name
                : item->name + "\n" + juce::String(item->serial).paddedLeft('0', 2));
            tiles.push_back(&tile);
        }
        if (insertion)
            tiles.insert(tiles.begin() + std::ptrdiff_t(*insertion), nullptr);
        const auto side = getSquareControlSize(*this);
        const auto gap = getUiGap(*this);
        activeBlock.setBounds(0, inactiveBlock.getBottom() + gap, getWidth(), side);
        const auto tileWidth = juce::jmax(0, getWidth() - side - gap);
        tiles.resize(juce::jmax(tiles.size(), size_t(chainRow.content.countFittingSlots(tileWidth))), nullptr);
        chainRow.layoutInline(activeBlock.getLocalBounds(), tiles);
    }

    void handleDrag(int id, bool fromCatalog, BoxTextButton& button, const juce::MouseEvent& event, bool released)
    {
        if (!dragging) return;
        auto& chainViewport = chainRow.viewport;
        auto& catalogViewport = catalogRow.viewport;
        const auto point = chainViewport.getLocalPoint(event.eventComponent, event.getPosition());
        const bool overChain = chainViewport.isShowing() && chainViewport.getLocalBounds().contains(point);
        const auto catalogPoint = catalogViewport.getLocalPoint(event.eventComponent, event.getPosition());
        const bool overCatalog = catalogViewport.isShowing() && catalogViewport.getLocalBounds().contains(catalogPoint);
        const auto side = getSquareControlSize(*this);
        const auto gap = getUiGap(*this);
        if (overChain && !released)
            chainViewport.autoScroll(point.x, point.y, 2 * gap, gap);
        const auto index = (point.x + chainViewport.getViewPositionX() + side / 2) / (side + gap);
        if (!released)
        {
            if (fromCatalog)
            {
                if (overCatalog)
                {
                    catalogViewport.autoScroll(catalogPoint.x, catalogPoint.y, 2 * gap, gap);
                    const auto from = int(std::find(catalogOrder.begin(), catalogOrder.end(), id) - catalogOrder.begin());
                    const auto slot = (catalogPoint.x + catalogViewport.getViewPositionX() + gap / 2) / (side + gap);
                    const auto to = juce::jlimit(0, int(catalogOrder.size()) - 1, slot);
                    if (ava::pinned_routing::moveToIndex(catalogOrder, from, to)) layoutCatalog();
                }
                const auto target = overChain ? std::optional<size_t>(juce::jlimit(0, int(order.size()), index)) : std::nullopt;
                if (insertion == target) return;
                insertion = target;
            }
            else if (overChain)
            {
                const auto from = int(std::find(order.begin(), order.end(), id) - order.begin());
                const auto to = juce::jlimit(0, int(order.size()) - 1, index);
                if (!ava::pinned_routing::moveToIndex(order, from, to)) return;
                ava::pinned_routing::moveToIndex(chainTiles, from, to);
            }
            else return;
            layoutChain();
            return;
        }

        const auto next = order;
        const auto dropIndex = size_t(juce::jlimit(0, int(order.size()), index));
        if (fromCatalog && overCatalog && catalogOrder != shell_editor_ui::inactiveModulesOrder(viewState))
            viewState.setProperty(shell_editor_ui::inactiveModulesOrderKey, module_order::serialise(catalogOrder), nullptr);
        if (insertion)
        {
            insertion.reset();
            layoutChain();
        }
        button.getProperties().set("routing.instance.dragging", false);
        button.repaint();
        juce::Component::beginDragAutoRepeat(0);
        // The tile under the pointer may be removed by this action after mouseUp.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ModuleList>(this),
                                       id, fromCatalog, overChain, overCatalog, dropIndex, next]
        {
            if (safe == nullptr) return;
            const auto callbacks = safe->actions;
            if (fromCatalog && overChain) callbacks.insert(id, dropIndex);
            else if (!fromCatalog && overCatalog) callbacks.remove(id, false);
            else if (!fromCatalog && overChain) callbacks.reorder(next);
            if (safe == nullptr) return;
            safe->dragging = false;
            safe->refreshItemsIfChanged(!fromCatalog);
        });
    }

    const Actions actions;
    juce::ValueTree& viewState;
    module_order::CatalogOrder catalogOrder;
    BackgroundClickComponent inactiveBlock, activeBlock;
    HeaderControlRow catalogRow {inactiveBlock};
    HeaderControlRow chainRow {activeBlock};
    std::vector<std::unique_ptr<BoxTextButton>> chainTiles;
    std::array<std::unique_ptr<BoxTextButton>, module_order::descriptions.size()> catalogTiles;
    std::vector<ModuleListItem> chainItems;
    Order order;
    std::optional<size_t> insertion;
    bool dragging = false;
};
