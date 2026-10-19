#pragma once

#include "Prompt.h"
#include "Controls.h"
#include "DisclosureAnimation.h"
#include "HoverAnimation.h"
#include "PopupListStyle.h"
#include "../crossover/ParameterIds.h"
#include "../modules/tls/BlockOrder.h"
#include "../modules/tls/GainOrder.h"

struct BlockArrangement {
    using Order=tls::blocks::Order;
    static constexpr auto descriptions=tls::blocks::descriptions;
    static constexpr const char* name="TLS-ARRANGE";
    static constexpr bool hasVisibility=true;
};
struct GainArrangement {
    using Order=tls::gain::Order;
    static constexpr auto descriptions=tls::gain::descriptions;
    static constexpr const char* name="TLS-GAIN-ARRANGE";
    static constexpr bool hasVisibility=false;
};

// Full-editor overlay: outside clicks are consumed, and only a released drag commits.
template <typename Definition>
class OrderArrangePrompt final : public PromptComponent
{
public:
    using Order = typename Definition::Order;
    using Id = typename Order::value_type;
    static constexpr size_t count=std::tuple_size_v<Order>;
    using Change = std::function<void(const Order&)>;
    OrderArrangePrompt(Order initial, Change previewIn, Change commitIn,
                       std::function<void()> dismissIn, std::function<void()> closeIn,
                       juce::AudioProcessorValueTreeState* bypassState=nullptr, size_t range=0)
        : order(initial), committed(initial), preview(std::move(previewIn)),
          commit(std::move(commitIn)), dismiss(std::move(dismissIn)), close(std::move(closeIn))
    {
        setName(Definition::name);
        setWantsKeyboardFocus(true);
        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(false,false);
        viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        list.setInterceptsMouseClicks(false,true);
        panel.addAndMakeVisible(viewport);
        addAndMakeVisible(panel);
        for(const auto& description:Definition::descriptions) {
            auto row=std::make_unique<Row>(*this,description.id,description.name);
            if(bypassState) {
                row->bindBypass(*bypassState,ava::crossover::parameters::makeRangeParameterId(range,description.bypassSuffix));
                if constexpr(Definition::hasVisibility)
                    row->bindVisibility(*bypassState,ava::crossover::parameters::makeRangeParameterId(range,description.hiddenSuffix));
            }
            list.addAndMakeVisible(*row);
            rows[size_t(description.id)]=std::move(row);
        }
        reveal.onUpdate=[this] {
            panel.setAlpha(float(reveal.value()));
            if(closing && !reveal.isRunning()) completeClose();
        };
    }
    ~OrderArrangePrompt() override { if(dismiss) dismiss(); }

    void resized() override
    {
        auto available=getLocalBounds().reduced(uiGap);
        const auto width=juce::jmin(240,juce::jmax(0,available.getWidth()));
        const auto height=juce::jmin(int(count)*itemHeight,juce::jmax(0,available.getHeight()));
        panel.setBounds(juce::Rectangle<int>(width,height).withCentre(available.getCentre()));
        viewport.setBounds(panel.getLocalBounds());
        list.setSize(viewport.getWidth(),int(count)*itemHeight);
        layoutRows();
        if(!started && !available.isEmpty()) { started=true; reveal.setExpanded(true); }
        panel.setAlpha(float(reveal.value()));
    }
    void paintOverChildren(juce::Graphics& g) override
    {
        if(reveal.value()<=0.0) return;
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(panel.getBounds());
        g.setColour(uiWhite.withMultipliedAlpha(float(reveal.value())));
        popup_list::paintFrame(g,panel.getBounds().toFloat());
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if(!panel.getBounds().contains(event.getPosition())) requestClose();
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if(key==juce::KeyPress::escapeKey) { requestClose(); return true; }
        return true; // Do not pass typing/shortcuts to controls behind the dialog.
    }

private:
    static constexpr int itemHeight=popup_list::itemHeight;
    struct Panel final : juce::Component {
        void paint(juce::Graphics& g) override { g.fillAll(uiGreyDark); }
    } panel;
    struct Row final : juce::Component {
        Row(OrderArrangePrompt& ownerIn,Id idIn,const char* textIn)
            : owner(ownerIn),id(idIn),text(textIn) { setName(text); }
        void paint(juce::Graphics& g) override {
            const auto selected=owner.dragging && owner.dragged==id;
            const auto hover=hoverAnimation.amount(pointerInside);
            ControlAppearance style(selected,hover);style.normalFrameVisible=false;
            style.paintBackground(g,getLocalBounds());
            style.paintFrame(g,getLocalBounds().withTrimmedRight(bypass?itemHeight:0).withTrimmedLeft(visibility?itemHeight:0));
            g.setColour(style.contentColour());
            g.setFont(makeUiFont());
            g.drawText(text,getLocalBounds().withTrimmedRight(bypass?itemHeight:0).withTrimmedLeft(visibility?itemHeight:0).reduced(8,0),juce::Justification::centred,false);
        }
        void paintOverChildren(juce::Graphics& g) override {
            g.setColour(uiWhite);
            if(visibility) {
                popup_list::fillLine(g,float(itemHeight)-popup_list::lineThickness,0.0f,float(itemHeight),float(getHeight()));
            }
            if(bypass) {
                popup_list::fillLine(g,float(getWidth()-itemHeight),0.0f,float(getWidth()-itemHeight)+popup_list::lineThickness,float(getHeight()));
            }
        }
        void bindBypass(juce::AudioProcessorValueTreeState& state,const juce::String& parameterId) {
            bypass=std::make_unique<BoxTextButton>(uiWhite);
            bypass->setName("BYPASS-"+text);
            bypass->setTooltip("BYPASS "+text);
            bypass->setClickingTogglesState(true);
            bypass->setBorderVisible(false);
            bypass->setTablerIcon("plug-off");
            attachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,parameterId,*bypass);
            addAndMakeVisible(*bypass);
        }
        void bindVisibility(juce::AudioProcessorValueTreeState& state,const juce::String& parameterId) {
            visibility=std::make_unique<BoxTextButton>(uiWhite);
            visibility->setName("HIDDEN-"+text);
            visibility->setTooltip("HIDE "+text);
            visibility->setClickingTogglesState(true);
            visibility->setBorderVisible(false);
            visibility->setTablerIcon("eye-off");
            visibilityAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,parameterId,*visibility);
            addAndMakeVisible(*visibility);
        }
        void resized() override {
            if(visibility) visibility->setBounds(getLocalBounds().removeFromLeft(itemHeight));
            if(bypass) bypass->setBounds(getLocalBounds().removeFromRight(itemHeight));
        }
        void mouseDown(const juce::MouseEvent& e) override {
            if(owner.closing || !e.mods.isLeftButtonDown()) return;
            owner.dragging=true; owner.dragged=id; repaint();
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            if(owner.dragging && !owner.closing) owner.setDragCursor(true);
            owner.drag(e);
        }
        void mouseUp(const juce::MouseEvent&) override { owner.drop(); }
        void mouseEnter(const juce::MouseEvent&) override { pointerInside=true;repaint(); }
        void mouseExit(const juce::MouseEvent&) override { pointerInside=false;repaint(); }
        OrderArrangePrompt& owner;
        Id id;
        juce::String text;
        HoverAnimation hoverAnimation{*this};
        bool pointerInside=false;
        std::unique_ptr<BoxTextButton> bypass, visibility;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment, visibilityAttachment;
    };
    void layoutRows() {
        for(size_t i=0;i<order.size();++i)
            rows[size_t(order[i])]->setBounds(0,int(i)*itemHeight,list.getWidth(),itemHeight);
        list.repaint();
    }
    void drag(const juce::MouseEvent& event) {
        if(!dragging || closing) return;
        const auto local=event.getEventRelativeTo(&viewport).getPosition();
        viewport.autoScroll(local.x,local.y,20,10);
        const auto y=event.getEventRelativeTo(&list).y;
        const auto target=size_t(juce::jlimit(0,int(order.size())-1,y/itemHeight));
        auto from=size_t(std::find(order.begin(),order.end(),dragged)-order.begin());
        if(from==target) return;
        const auto id=order[from];
        if(from<target) std::move(order.begin()+std::ptrdiff_t(from+1),order.begin()+std::ptrdiff_t(target+1),order.begin()+std::ptrdiff_t(from));
        else std::move_backward(order.begin()+std::ptrdiff_t(target),order.begin()+std::ptrdiff_t(from),order.begin()+std::ptrdiff_t(from+1));
        order[target]=id;
        layoutRows();
        if(preview) preview(order);
    }
    void setDragCursor(bool active) {
        // All rows use the same cursor while dragging, including when the pointer crosses a row.
        const auto cursor=active?juce::MouseCursor::DraggingHandCursor:juce::MouseCursor::NormalCursor;
        setMouseCursor(cursor);
        for(auto& row:rows) row->setMouseCursor(cursor);
    }
    void drop() {
        if(!dragging || closing) return;
        dragging=false;
        setDragCursor(false);
        if(order!=committed) {
            committed=order;
            if(commit) commit(order);
        }
        list.repaint();
    }
    void requestClose() {
        if(closing || closePending) return;
        closing=true;
        dragging=false;
        setDragCursor(false);
        reveal.setExpanded(false);
        if(!reveal.isRunning()) completeClose();
    }
    void completeClose() {
        if(closePending) return;
        if(dismiss) { auto fn=std::move(dismiss); fn(); }
        requestPromptClose(*this,closePending,close);
    }
    struct ListContent final : juce::Component {
        void paintOverChildren(juce::Graphics& g) override {
            g.setColour(uiWhite);
            popup_list::paintSeparators(g,getWidth(),int(count));
        }
    } list;
    juce::Viewport viewport;
    std::array<std::unique_ptr<Row>,count> rows;
    Order order,committed;
    Id dragged=Id(0);
    Change preview,commit;
    std::function<void()> dismiss,close;
    DisclosureAnimation reveal;
    bool started=false,closing=false,closePending=false,dragging=false;
};

using BlockArrangePrompt=OrderArrangePrompt<BlockArrangement>;
using GainArrangePrompt=OrderArrangePrompt<GainArrangement>;
