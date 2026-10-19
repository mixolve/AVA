#include "SvgExport.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include <vector>
#include <unordered_map>

namespace svg_export
{
namespace
{
juce::String number(float value) { return juce::String(value, 5); }
juce::String parameterIdFor(juce::Component& component)
{
    const auto property=component.getProperties()["oscParameterId"].toString();
    if (property.isNotEmpty()) return property;
    if (auto* control=dynamic_cast<ParameterControl*>(&component)) return control->getParameterId();
    if (auto* choice=dynamic_cast<ChoiceControl*>(&component)) return choice->getParameterId();
    return {};
}
juce::String sanitizeSvgId(juce::String text)
{
    text=text.replace(" ","-").replace("/","-");
    text=text.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-");
    while (text.contains("--")) text=text.replace("--","-");
    while (text.startsWithChar('-')) text=text.substring(1);
    while (text.endsWithChar('-')) text=text.dropLastCharacters(1);
    if (text.isEmpty()||juce::CharacterFunctions::isDigit(text[0])) text="p-"+text;
    return text;
}
juce::String componentKind(juce::Component& component)
{
    if (dynamic_cast<juce::Slider*>(&component)!=nullptr) return "slider";
    if (dynamic_cast<juce::Button*>(&component)!=nullptr) return "button";
    if (dynamic_cast<juce::Label*>(&component)!=nullptr) return "label";
    if (dynamic_cast<juce::ComboBox*>(&component)!=nullptr) return "combo";
    return "part";
}
juce::String matrix(const juce::AffineTransform& t)
{
    return "matrix(" + number(t.mat00) + " " + number(t.mat10) + " "
        + number(t.mat01) + " " + number(t.mat11) + " " + number(t.mat02) + " " + number(t.mat12) + ")";
}
juce::String pathData(const juce::Path& path)
{
    juce::String result;
    for (juce::Path::Iterator it(path); it.next();)
    {
        const auto point = [](float x, float y) { return number(x) + " " + number(y) + " "; };
        switch (it.elementType)
        {
            case juce::Path::Iterator::startNewSubPath: result += "M" + point(it.x1,it.y1); break;
            case juce::Path::Iterator::lineTo: result += "L" + point(it.x1,it.y1); break;
            case juce::Path::Iterator::quadraticTo: result += "Q" + point(it.x1,it.y1) + point(it.x2,it.y2); break;
            case juce::Path::Iterator::cubicTo: result += "C" + point(it.x1,it.y1) + point(it.x2,it.y2) + point(it.x3,it.y3); break;
            case juce::Path::Iterator::closePath: result += "Z "; break;
        }
    }
    return result;
}
class Context final : public juce::LowLevelGraphicsContext
{
public:
    explicit Context(juce::Rectangle<int> bounds) { state.clipBounds = bounds.toFloat(); }
    juce::String defs, content;
    // Skip layout wrappers so Illustrator exposes controls as flat selectable objects.
    static bool isStructuralIdentifier(const juce::String& identifier)
    {
        return identifier.isEmpty()
            || identifier == "editor"
            || identifier == "viewport"
            || identifier == "viewport-content"
            || identifier.startsWith("module-")
            || identifier.startsWith("page-")
            || identifier.startsWith("group-");
    }
    bool paintComponent(juce::Component& component, juce::Graphics& graphics,
                        const juce::String& parentParameterId = {})
    {
        const auto contentStart=content.length();
        const auto defsStart=defs.length();
        const auto clipsStart=rectClipCache.size();
        const auto parameterId=parameterIdFor(component);
        const auto parameterContext=parameterId.isNotEmpty() ? parameterId : parentParameterId;
        juce::String identifier=parameterId;
        if (identifier.isEmpty()) identifier=component.getName();
        if (identifier.isEmpty()) identifier=component.getComponentID();
        if (identifier.isEmpty() && parameterContext.isNotEmpty())
            identifier=parameterContext+"-"+componentKind(component);
        if (identifier.isEmpty())
        {
            if (auto* button=dynamic_cast<juce::Button*>(&component)) identifier=button->getButtonText();
            else if (auto* label=dynamic_cast<juce::Label*>(&component)) identifier=label->getText();
        }
        juce::XmlElement group("g");
        bool structuralContainer=false;
        group.setAttribute("id", identifier.isNotEmpty()
            ? uniqueId(sanitizeSvgId(identifier))
            : uniqueId("component-" + juce::String(++componentSerial)));
        juce::String label=component.getName();
        if (auto* button=dynamic_cast<juce::Button*>(&component))
        {
            group.setAttribute("data-component", "button");
            if (label.isEmpty()) label=button->getButtonText();
        }
        else if (auto* text=dynamic_cast<juce::Label*>(&component))
        {
            group.setAttribute("data-component", "label");
            if (label.isEmpty()) label=text->getText();
        }
        else if (parameterId.isNotEmpty()) group.setAttribute("data-component", "parameter");
        else
        {
            group.setAttribute("data-component", "container");
            structuralContainer=isStructuralIdentifier(identifier);
        }
        if (parameterId.isNotEmpty()) group.setAttribute("data-parameter", parameterId);
        if (label.isEmpty()) label=component.getComponentID();
        if (label.isEmpty()) label=parameterId;
        if (label.isEmpty()) label=identifier;
        if (label.isNotEmpty()) group.setAttribute("data-name",label);
        auto opening=group.toString(juce::XmlElement::TextFormat().withoutHeader());
        opening=opening.replace("/>",">");
        if (structuralContainer) opening={};
        content += opening;
        {
            const juce::Graphics::ScopedSaveState saved(graphics);
            component.paint(graphics);
        }
        for (int i=0;i<component.getNumChildComponents();++i)
        {
            auto* child=component.getChildComponent(i);
            if (!child->isVisible() || child->getAlpha()<=0.f) continue;
            const auto childContentStart=content.length();
            const auto childDefsStart=defs.length();
            const auto childClipsStart=rectClipCache.size();
            const juce::Graphics::ScopedSaveState saved(graphics);
            graphics.addTransform(juce::AffineTransform::translation(
                static_cast<float>(child->getX()),static_cast<float>(child->getY()))
                .followedBy(child->getTransform()));
            if (!child->isPaintingUnclipped() && !graphics.reduceClipRegion(child->getLocalBounds()))
            {
                defs=defs.substring(0,childDefsStart);
                rollbackRectClips(childClipsStart);
                continue;
            }
            const auto transparent=child->getAlpha()<1.f;
            if (transparent) graphics.beginTransparencyLayer(child->getAlpha());
            const auto painted=paintComponent(*child,graphics,parameterContext);
            if (transparent) graphics.endTransparencyLayer();
            if (!painted)
            {
                content=content.substring(0,childContentStart);
                defs=defs.substring(0,childDefsStart);
                rollbackRectClips(childClipsStart);
            }
        }
        {
            const juce::Graphics::ScopedSaveState saved(graphics);
            component.paintOverChildren(graphics);
        }
        if (content.length()==contentStart+opening.length())
        {
            content=content.substring(0,contentStart);
            defs=defs.substring(0,defsStart);
            rollbackRectClips(clipsStart);
            return false;
        }
        if (opening.isNotEmpty()) content += "</g>\n";
        return true;
    }
    bool isVectorDevice() const override { return true; }
    void setOrigin(juce::Point<int> p) override { addTransform(juce::AffineTransform::translation(static_cast<float>(p.x),static_cast<float>(p.y))); }
    void addTransform(const juce::AffineTransform& t) override { state.transform = t.followedBy(state.transform); }
    float getPhysicalPixelScaleFactor() const override { return 1.f; }
    void pushClipRectangle(const juce::Rectangle<float>& rect)
    {
        // JUCE reads clipBounds while painting, including fillAll(). Accumulate rectangle
        // intersections here so emit() avoids deep SVG clip groups; path/mask clips still nest.
        state.clipBounds=state.clipBounds.getIntersection(rect);
        state.hasRectClip=true;
    }
    bool clipToRectangle(const juce::Rectangle<int>& r) override
    { pushClipRectangle(r.toFloat().transformedBy(state.transform)); return !isClipEmpty(); }
    bool clipToRectangleList(const juce::RectangleList<int>& list) override
    {
        if(list.getNumRectangles()==1)
        {
            pushClipRectangle(list.getRectangle(0).toFloat().transformedBy(state.transform));
            return !isClipEmpty();
        }
        juce::Path p; for(auto r:list) p.addRectangle(r); clipToPath(p,{});
        return !isClipEmpty();
    }
    void excludeClipRectangle(const juce::Rectangle<int>& r) override
    {
        juce::Path p; p.setUsingNonZeroWinding(false);p.addRectangle(getClipBounds());p.addRectangle(r);
        clipToPath(p,{});
    }
    void clipToPath(const juce::Path& path,const juce::AffineTransform& t) override
    {
        state.onlyRectClips=false;
        auto p=path;p.applyTransform(t.followedBy(state.transform));
        state.clipBounds=state.clipBounds.getIntersection(p.getBounds());
        const auto id=nextId();
        defs += "<clipPath id=\""+id+"\" clipPathUnits=\"userSpaceOnUse\"><path d=\""+pathData(p)
            +"\" clip-rule=\""+(p.isUsingNonZeroWinding()?"nonzero":"evenodd")+"\"/></clipPath>\n";
        state.clips.push_back("clip-path=\"url(#"+id+")\"");
    }
    void clipToImageAlpha(const juce::Image& image,const juce::AffineTransform& t) override
    {
        state.onlyRectClips=false;
        const auto id=nextId();
        defs += "<mask id=\""+id+"\" maskUnits=\"userSpaceOnUse\" maskContentUnits=\"userSpaceOnUse\" style=\"mask-type:alpha\">"
            +imageElement(image,t.followedBy(state.transform),true)+"</mask>\n";
        state.clips.push_back("mask=\"url(#"+id+")\"");
        state.clipBounds=state.clipBounds.getIntersection(image.getBounds().toFloat().transformedBy(t.followedBy(state.transform)));
    }
    bool clipRegionIntersects(const juce::Rectangle<int>& r) override
    { return state.clipBounds.intersects(r.toFloat().transformedBy(state.transform)); }
    juce::Rectangle<int> getClipBounds() const override
    { return state.clipBounds.transformedBy(state.transform.inverted()).getSmallestIntegerContainer(); }
    bool isClipEmpty() const override {return state.clipBounds.isEmpty();}
    void saveState() override { stack.push_back(state); }
    void restoreState() override { if(!stack.empty()){state=stack.back();stack.pop_back();} }
    void beginTransparencyLayer(float opacity) override {content += "<g opacity=\""+number(opacity)+"\">\n";saveState();}
    void endTransparencyLayer() override {restoreState();content += "</g>\n";}
    void setFill(const juce::FillType& fill) override {state.fill=fill;}
    void setOpacity(float opacity) override {state.fill.setOpacity(opacity);}
    void setInterpolationQuality(juce::Graphics::ResamplingQuality) override {}
    void fillRect(const juce::Rectangle<int>& r,bool) override {fillRect(r.toFloat());}
    void fillRect(const juce::Rectangle<float>& r) override {juce::Path p;p.addRectangle(r);fillPath(p,{});}
    void fillRectList(const juce::RectangleList<float>& list) override {for(auto r:list)fillRect(r);}
    void fillPath(const juce::Path& path,const juce::AffineTransform& t) override
    {
        if(isClipEmpty())return;
        emit("<path d=\""+pathData(path)+"\" transform=\""+matrix(t.followedBy(state.transform))
            +"\" fill-rule=\""+(path.isUsingNonZeroWinding()?"nonzero":"evenodd")+"\" "+fillAttributes()+"/>\n");
    }
    void drawImage(const juce::Image& image,const juce::AffineTransform& t) override
    {emit("<g opacity=\""+number(state.fill.getOpacity())+"\">"+imageElement(image,t.followedBy(state.transform),false)+"</g>\n");}
    void drawLine(const juce::Line<float>& line) override {drawLineWithThickness(line,1.f);}
    void setFont(const juce::Font& font) override {state.font=font;}
    const juce::Font& getFont() override {return state.font;}
    void drawGlyphs(juce::Span<const uint16_t> glyphs,juce::Span<const juce::Point<float>> positions,const juce::AffineTransform& t) override
    {
        const auto typeface=state.font.getTypefacePtr();
        auto& characters = glyphCharacters[typeface.get()];
        if (characters.empty())
        {
            // JUCE passes shaped glyph IDs to its low-level renderer. Recover
            // their Unicode characters from the typeface's nominal cmap.
            for (juce::juce_wchar codepoint = 32; codepoint <= 0xffff; ++codepoint)
                if (codepoint < 0xd800 || codepoint > 0xdfff)
                    if (const auto glyph = typeface->getNominalGlyphForCodepoint(codepoint); glyph && *glyph != 0)
                        characters.try_emplace(*glyph, codepoint);
        }
        juce::String text, xPositions, yPositions;
        const auto horizontalScale=state.font.getHorizontalScale();
        for(size_t i=0;i<glyphs.size();++i)
        {
            const auto found=characters.find(glyphs[i]);
            text += juce::String::charToString(found!=characters.end() ? found->second : 0xfffd);
            xPositions += number(positions[i].x / horizontalScale) + " ";
            yPositions += number(positions[i].y) + " ";
        }
        juce::XmlElement element("text");
        element.setAttribute("xml:space", "preserve");
        element.setAttribute("font-family", typeface->getName());
        element.setAttribute("font-size", number(state.font.getHeight() * state.font.getHeightToPointsFactor()));
        const auto style=typeface->getStyle().toLowerCase();
        element.setAttribute("font-weight", style.contains("bold") ? "700" : style.contains("medium") ? "500" : "400");
        element.setAttribute("font-style", state.font.isItalic() ? "italic" : "normal");
        element.setAttribute("x", xPositions.trimEnd());
        element.setAttribute("y", yPositions.trimEnd());
        element.setAttribute("transform", matrix(juce::AffineTransform::scale(horizontalScale,1.f).followedBy(t).followedBy(state.transform)));
        element.addTextElement(text);
        auto markup=element.toString(juce::XmlElement::TextFormat().withoutHeader());
        markup=markup.replaceFirstOccurrenceOf("<text ", "<text " + fillAttributes() + " ");
        emit(markup);

    }
    std::unique_ptr<juce::ImageType> getPreferredImageTypeForTemporaryImages() const override
    {return std::make_unique<juce::SoftwareImageType>();}
    juce::uint64 getFrameId() const override {return 1;}
private:
    struct State
    {
        juce::AffineTransform transform;
        juce::Rectangle<float> clipBounds;
        juce::FillType fill;
        juce::Font font {juce::FontOptions()};
        std::vector<juce::String> clips;
        bool onlyRectClips=true;
        bool hasRectClip=false;
    } state;
    std::vector<State> stack;
    std::unordered_map<const juce::Typeface*, std::unordered_map<uint32_t, juce::juce_wchar>> glyphCharacters;
    int componentSerial=0;
    int serial=0;
    std::unordered_map<std::string,int> usedIds;
    std::vector<std::pair<std::string,juce::String>> rectClipCache;
    std::unordered_map<std::string,size_t> rectClipCacheIndex;
    juce::String uniqueId(const juce::String& base)
    {
        auto& count=usedIds[base.toStdString()];
        ++count;
        return count==1 ? base : base + "-" + juce::String(count);
    }
    juce::String nextId(){return "ava-svg-"+juce::String(++serial);}
    juce::String rectClipAttribute(const juce::Rectangle<float>& rect)
    {
        const auto key=number(rect.getX())+","+number(rect.getY())+","
            +number(rect.getWidth())+","+number(rect.getHeight());
        const auto keyString=key.toStdString();
        if(const auto found=rectClipCacheIndex.find(keyString);found!=rectClipCacheIndex.end())
            return rectClipCache[found->second].second;
        const auto id=nextId();
        juce::Path p;p.addRectangle(rect);
        defs += "<clipPath id=\""+id+"\" clipPathUnits=\"userSpaceOnUse\"><path d=\""+pathData(p)+"\" clip-rule=\"nonzero\"/></clipPath>\n";
        juce::String attribute="clip-path=\"url(#"+id+")\"";
        rectClipCacheIndex.emplace(keyString,rectClipCache.size());
        rectClipCache.emplace_back(keyString,attribute);
        return attribute;
    }
    void rollbackRectClips(size_t count)
    {
        while(rectClipCache.size()>count)
        {
            rectClipCacheIndex.erase(rectClipCache.back().first);
            rectClipCache.pop_back();
        }
    }
    void emit(const juce::String& element)
    {
        if(isClipEmpty())return;
        const auto rectWrapper = state.hasRectClip ? rectClipAttribute(state.clipBounds) : juce::String();
        if(state.onlyRectClips)
        {
            if(rectWrapper.isEmpty()) content += element;
            else content += "<g "+rectWrapper+">"+element+"</g>";
            content += "\n";
            return;
        }
        if(rectWrapper.isNotEmpty()) content += "<g "+rectWrapper+">";
        for(const auto& clip:state.clips)content += "<g "+clip+">";
        content += element;
        for(size_t i=0;i<state.clips.size();++i)content += "</g>";
        if(rectWrapper.isNotEmpty()) content += "</g>";
        content += "\n";
    }
    juce::String fillAttributes()
    {
        const auto opacity="fill-opacity=\""+number(state.fill.getOpacity())+"\"";
        if(state.fill.gradient!=nullptr)
        {
            const auto& gradient=*state.fill.gradient;const auto id=nextId();
            const auto radial=gradient.isRadial;
            defs += "<"+juce::String(radial?"radialGradient":"linearGradient")+" id=\""+id+"\" gradientUnits=\"userSpaceOnUse\" gradientTransform=\""+matrix(state.fill.transform)+"\" ";
            if(radial)defs += "cx=\""+number(gradient.point1.x)+"\" cy=\""+number(gradient.point1.y)+"\" r=\""+number(gradient.point1.getDistanceFrom(gradient.point2))+"\">";
            else defs += "x1=\""+number(gradient.point1.x)+"\" y1=\""+number(gradient.point1.y)+"\" x2=\""+number(gradient.point2.x)+"\" y2=\""+number(gradient.point2.y)+"\">";
            for(int i=0;i<gradient.getNumColours();++i)
            {
                const auto c=gradient.getColour(i);
                defs += "<stop offset=\""+juce::String(gradient.getColourPosition(i),5)+"\" stop-color=\"#"+c.toDisplayString(false)+"\" stop-opacity=\""+number(c.getFloatAlpha())+"\"/>";
            }
            defs += "</"+juce::String(radial?"radialGradient":"linearGradient")+">\n";
            return "fill=\"url(#"+id+")\" "+opacity;
        }
        return "fill=\"#"+state.fill.colour.toDisplayString(false)+"\" "+opacity;
    }
    juce::String imageElement(const juce::Image& image,const juce::AffineTransform& t,bool mask)
    {
        if(!image.isValid())return {};
        if(auto xml=juce::XmlDocument::parse(image.getProperties()->getWithDefault("ava-source-svg",{}).toString()))
        {
            xml->setAttribute("width",image.getWidth());xml->setAttribute("height",image.getHeight());
            if(mask){xml->setAttribute("stroke","#ffffff");}
            return "<g transform=\""+matrix(t)+"\">"+xml->toString(juce::XmlElement::TextFormat().withoutHeader())+"</g>";
        }
        juce::MemoryOutputStream stream;juce::PNGImageFormat().writeImageToStream(image,stream);
        return "<image width=\""+juce::String(image.getWidth())+"\" height=\""+juce::String(image.getHeight())+"\" transform=\""+matrix(t)+"\" xlink:href=\"data:image/png;base64,"+juce::Base64::toBase64(stream.getData(),stream.getDataSize())+"\"/>";
    }
};

class ViewportExportLayout
{
public:
    explicit ViewportExportLayout(juce::Component& rootIn) : root(rootIn), originalBounds(root.getBounds())
    {
        collect(root);
        int extra = 0;
        for (const auto& item : views)
            if (auto* content = item.viewport->getViewedComponent())
                extra = juce::jmax(extra, content->getHeight() - item.viewport->getHeight());
        // Editors/windows provide a virtual export layout: never resize their native peer.
        if (extra > 0 && dynamic_cast<ExpandableContent*>(&root) == nullptr && !root.isOnDesktop())
            root.setSize(root.getWidth(), root.getHeight() + extra);
        for (const auto& item : views)
            if (item.viewport != nullptr) item.viewport->setViewPosition(0, 0);
    }
    ~ViewportExportLayout()
    {
        if (root.getBounds() != originalBounds) root.setBounds(originalBounds);
        for (const auto& item : views)
            if (item.viewport != nullptr) item.viewport->setViewPosition(item.position);
    }
private:
    void collect(juce::Component& component)
    {
        if (auto* viewport = dynamic_cast<juce::Viewport*>(&component))
            views.push_back({viewport, viewport->getViewPosition()});
        for (auto* child : component.getChildren())
            if (child->isVisible()) collect(*child);
    }
    struct View { juce::Component::SafePointer<juce::Viewport> viewport; juce::Point<int> position; };
    juce::Component& root;
    juce::Rectangle<int> originalBounds;
    std::vector<View> views;
};

void collectExpandableContent(juce::Component& component,std::vector<ExpandableContent*>& result)
{
    if(auto* expandable=dynamic_cast<ExpandableContent*>(&component))
        result.push_back(expandable);
    for(int index=0;index<component.getNumChildComponents();++index)
        if(auto* child=component.getChildComponent(index))
            if(child->isVisible())
                collectExpandableContent(*child,result);
}
}
juce::String render(juce::Component& component)
{
    ViewportExportLayout viewportLayout(component);
    std::vector<ExpandableContent*> expandableContent;
    collectExpandableContent(component,expandableContent);
    for(auto* expandable:expandableContent)
        expandable->expandContentForSvgExport();

    auto canvasHeight=component.getHeight();
    for(auto* expandable:expandableContent)
        canvasHeight=juce::jmax(canvasHeight,expandable->getSvgExportCanvasHeight());
    for(auto* expandable:expandableContent)
        expandable->prepareSvgExportLayout(canvasHeight);

    Context context({0,0,component.getWidth(),canvasHeight});juce::Graphics graphics(context);
    context.paintComponent(component,graphics);

    for(auto* expandable:expandableContent)
        expandable->finishSvgExportLayout();
    for(auto* expandable:expandableContent)
        expandable->restoreContentAfterSvgExport();
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" version=\"1.1\" width=\""
        +juce::String(component.getWidth())+"\" height=\""+juce::String(canvasHeight)+"\" viewBox=\"0 0 "
        +juce::String(component.getWidth())+" "+juce::String(canvasHeight)+"\">\n<defs>"+context.defs+"</defs>\n"+context.content+"</svg>\n";
}
void chooseFile(juce::Component& component)
{
    const auto svg=render(component);
    auto chooser=std::make_shared<juce::FileChooser>("Export SVG",juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("AVA.svg"),"*.svg");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [chooser,svg](const juce::FileChooser& dialog)
        {
            const auto file=dialog.getResult();if(file==juce::File())return;
            const auto target=file.withFileExtension("svg");
            if(!target.replaceWithText(svg))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"SVG export","Unable to save SVG.");
        });
}
}
