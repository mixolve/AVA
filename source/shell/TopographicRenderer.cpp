// Radiant / Paul Bakaus (MIT).
#include "TopographicRenderer.h"
#include "AnimationColour.h"
#include "Style.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>
#include <utility>
#include <optional>

namespace
{
// This is the original permutation generator and 3D Simplex Noise, translated
// directly from JavaScript. Keep the gradient order and seed 73 unchanged.
class SimplexNoise
{
public:
    explicit SimplexNoise(double seed = 73)
    {
        std::array<int, 256> p {};
        for (int i = 0; i < 256; ++i) p[static_cast<size_t>(i)] = i;
        for (int i = 255; i > 0; --i)
        {
            seed = std::fmod(seed * 16807, 2147483647);
            const auto j = static_cast<int>(seed) % (i + 1);
            std::swap(p[static_cast<size_t>(i)], p[static_cast<size_t>(j)]);
        }
        for (int i = 0; i < 512; ++i)
        {
            perm[static_cast<size_t>(i)] = p[static_cast<size_t>(i & 255)];
            permMod12[static_cast<size_t>(i)] = perm[static_cast<size_t>(i)] % 12;
        }
    }

    double noise3D(double xin, double yin, double zin) const
    {
        constexpr double F3 = 1.0 / 3.0, G3 = 1.0 / 6.0;
        const auto s = (xin + yin + zin) * F3;
        const auto i = static_cast<int>(std::floor(xin + s));
        const auto j = static_cast<int>(std::floor(yin + s));
        const auto k = static_cast<int>(std::floor(zin + s));
        const auto t = (i + j + k) * G3;
        const auto x0 = xin - (i - t), y0 = yin - (j - t), z0 = zin - (k - t);
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0)
        {
            if (y0 >= z0)      { i1=1; j1=0; k1=0; i2=1; j2=1; k2=0; }
            else if (x0 >= z0) { i1=1; j1=0; k1=0; i2=1; j2=0; k2=1; }
            else               { i1=0; j1=0; k1=1; i2=1; j2=0; k2=1; }
        }
        else
        {
            if (y0 < z0)      { i1=0; j1=0; k1=1; i2=0; j2=1; k2=1; }
            else if (x0 < z0) { i1=0; j1=1; k1=0; i2=0; j2=1; k2=1; }
            else              { i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; }
        }
        const auto x1=x0-i1+G3, y1=y0-j1+G3, z1=z0-k1+G3;
        const auto x2=x0-i2+2*G3, y2=y0-j2+2*G3, z2=z0-k2+2*G3;
        const auto x3=x0-1+3*G3, y3=y0-1+3*G3, z3=z0-1+3*G3;
        const auto ii=i&255, jj=j&255, kk=k&255;
        const auto corner = [this](double x, double y, double z, int a, int b, int c)
        {
            auto attenuation = .6 - x*x - y*y - z*z;
            if (attenuation < 0) return 0.0;
            attenuation *= attenuation;
            const auto gi = permMod12[static_cast<size_t>(a + perm[static_cast<size_t>(b + perm[static_cast<size_t>(c)])])];
            const auto& g = gradients[static_cast<size_t>(gi)];
            return attenuation * attenuation * (g[0]*x + g[1]*y + g[2]*z);
        };
        return 32 * (corner(x0,y0,z0,ii,jj,kk) + corner(x1,y1,z1,ii+i1,jj+j1,kk+k1)
                   + corner(x2,y2,z2,ii+i2,jj+j2,kk+k2) + corner(x3,y3,z3,ii+1,jj+1,kk+1));
    }

    double fbm(double x, double y, double z) const
    {
        double value=0, amplitude=1, frequency=1, sum=0;
        for (int octave=0; octave<4; ++octave)
        {
            value += noise3D(x*frequency, y*frequency, z) * amplitude;
            sum += amplitude;
            amplitude *= .5;
            frequency *= 2;
        }
        return value / sum;
    }

private:
    std::array<int, 512> perm {}, permMod12 {};
    static constexpr std::array<std::array<int, 3>, 12> gradients {{
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0}, {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
        {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}
    }};
};

constexpr int cellSize = 8;
constexpr std::array<std::array<int, 4>, 16> edgeTable {{
    {-1,-1,-1,-1}, {3,2,-1,-1}, {2,1,-1,-1}, {3,1,-1,-1},
    {1,0,-1,-1}, {1,0,3,2}, {2,0,-1,-1}, {3,0,-1,-1},
    {0,3,-1,-1}, {0,2,-1,-1}, {0,3,2,1}, {0,1,-1,-1},
    {1,3,-1,-1}, {1,2,-1,-1}, {2,3,-1,-1}, {-1,-1,-1,-1}
}};

std::pair<int, int> contourRange(double tl, double tr, double br, double bl, int density)
{
    // Include boundary levels conservatively; the original case table still decides crossings.
    const auto low=std::min({tl,tr,br,bl}), high=std::max({tl,tr,br,bl});
    return { std::max(0,static_cast<int>(std::floor(low*(density+1)))-1),
             std::min(density-1,static_cast<int>(std::ceil(high*(density+1)))-1) };
}

juce::Point<float> edgePoint(int edge, int x, int y, double tl, double tr, double br, double bl, double threshold)
{
    const auto interp = [threshold](double a, double b) { return std::abs(b-a)<.0001 ? .5 : (threshold-a)/(b-a); };
    switch (edge)
    {
        case 0: return { static_cast<float>(x + interp(tl,tr)*cellSize), static_cast<float>(y) };
        case 1: return { static_cast<float>(x+cellSize), static_cast<float>(y + interp(tr,br)*cellSize) };
        case 2: return { static_cast<float>(x + interp(bl,br)*cellSize), static_cast<float>(y+cellSize) };
        default:return { static_cast<float>(x), static_cast<float>(y + interp(tl,bl)*cellSize) };
    }
}

juce::Colour lerpColour(juce::Colour a, juce::Colour b, double t)
{
    const auto channel = [t](int x, int y) { return static_cast<juce::uint8>(std::lround(x+(y-x)*t)); };
    return juce::Colour::fromRGB(channel(a.getRed(),b.getRed()), channel(a.getGreen(),b.getGreen()),
                                channel(a.getBlue(),b.getBlue()));
}
}

struct TopographicRenderer::Impl : juce::Thread
{
    explicit Impl(TopographicRenderer& rendererIn) : Thread("Topographic preview"),renderer(rendererIn) {}
    ~Impl() override { stopThread(-1); }

    struct PreviewRequest
    {
        int canvasWidth,canvasHeight,pixelWidth,pixelHeight,density,colour;
        double time;
        float scale,thickness;
        bool equals(const PreviewRequest& other) const
        {
            return canvasWidth==other.canvasWidth && canvasHeight==other.canvasHeight
                && pixelWidth==other.pixelWidth && pixelHeight==other.pixelHeight
                && density==other.density && colour==other.colour
                && std::abs(time-other.time)<std::numeric_limits<double>::epsilon()
                && std::abs(thickness-other.thickness)<std::numeric_limits<float>::epsilon()
                && std::abs(scale-other.scale)<std::numeric_limits<float>::epsilon();
        }
    };
    TopographicRenderer& renderer;
    juce::CriticalSection previewMutex;
    std::optional<PreviewRequest> latestPreview,pendingPreview;
    juce::Image previewImage;

    juce::Image requestPreview(PreviewRequest request)
    {
        const juce::ScopedLock lock(previewMutex);
        if (!latestPreview || !latestPreview->equals(request))
        {
            latestPreview=request; pendingPreview=request;
            if (!isThreadRunning()) startThread(juce::Thread::Priority::normal);
            notify();
        }
        return previewImage;
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            wait(-1);
            while (!threadShouldExit())
            {
                std::optional<PreviewRequest> request;
                {
                    const juce::ScopedLock lock(previewMutex);
                    request=pendingPreview; pendingPreview.reset();
                }
                if (!request) break;
                // Rasterize the exact paths at the preview's physical pixel size.
                // No GUI or GL-context lock is held here; pending updates are coalesced.
                juce::Image image(juce::Image::RGB,request->pixelWidth,request->pixelHeight,true);
                {
                    juce::Graphics graphics(image);
                    graphics.addTransform(juce::AffineTransform::scale(
                        static_cast<float>(request->pixelWidth)/static_cast<float>(request->canvasWidth),
                        static_cast<float>(request->pixelHeight)/static_cast<float>(request->canvasHeight)));
                    renderer.paint(graphics,request->canvasWidth,request->canvasHeight,
                        request->time,request->scale,request->density,request->colour,request->thickness);
                }
                {
                    const juce::ScopedLock lock(previewMutex);
                    previewImage=std::move(image);
                }
            }
        }
    }
    struct Segment { int aKey,bKey; juce::Point<float> a,b; bool used=false; };
    struct Contour
    {
        juce::Path path;
        juce::Colour colour;
        float glowWidth=0,sharpWidth=0,glowAlpha=0,sharpAlpha=0;
        std::vector<Segment> segments;
    };
    struct Vertex
    {
        juce::Point<float> point;
        int first=-1,second=-1;
        size_t generation=0;
    };
    struct Label { juce::Point<float> point; float angle; int elevation; juce::Colour colour; float alpha; };
    SimplexNoise noise;
    std::vector<float> field;
    std::vector<Contour> contours;
    std::vector<Label> labels;
    juce::CriticalSection mutex;
    double previousTime = -1;
    float previousScale = 0;
    int width=0, height=0, cols=0, rows=0, previousDensity=0, labelDensity=0;
    size_t fieldGeneration=0, geometryGeneration=0, labelGeneration=0;
    juce::Rectangle<int> geometryCells;
    std::vector<Vertex> vertices;
    std::vector<int> activeVertices;
    size_t vertexGeneration=0;

    int edgeKey(int edge,int x,int y) const
    {
        const auto horizontalEdges=rows*(cols-1);
        switch (edge)
        {
            case 0: return y*(cols-1)+x;
            case 1: return horizontalEdges+y*cols+x+1;
            case 2: return (y+1)*(cols-1)+x;
            default:return horizontalEdges+y*cols+x;
        }
    }

    void connectSegments(Contour& contour)
    {
        ++vertexGeneration;
        vertices.resize(static_cast<size_t>(rows*(cols-1)+(rows-1)*cols));
        activeVertices.clear();
        const auto connect=[this](int key,juce::Point<float> point,int segment)
        {
            auto& vertex=vertices[static_cast<size_t>(key)];
            if (vertex.generation!=vertexGeneration)
            {
                vertex={point,segment,-1,vertexGeneration};
                activeVertices.push_back(key);
            }
            else
            {
                jassert(vertex.second<0);
                vertex.second=segment;
            }
        };
        for (size_t i=0;i<contour.segments.size();++i)
        {
            const auto& segment=contour.segments[i];
            connect(segment.aKey,segment.a,static_cast<int>(i));
            connect(segment.bKey,segment.b,static_cast<int>(i));
        }
        const auto follow=[this,&contour](int firstVertex,int firstSegment)
        {
            auto key=firstVertex, index=firstSegment;
            contour.path.startNewSubPath(vertices[static_cast<size_t>(key)].point);
            while (index>=0 && !contour.segments[static_cast<size_t>(index)].used)
            {
                auto& segment=contour.segments[static_cast<size_t>(index)];
                segment.used=true;
                key=segment.aKey==key?segment.bKey:segment.aKey;
                const auto& vertex=vertices[static_cast<size_t>(key)];
                contour.path.lineTo(vertex.point);
                if (key==firstVertex) { contour.path.closeSubPath(); break; }
                index=vertex.first==index?vertex.second:vertex.first;
            }
        };
        // Open paths start at a degree-one boundary; remaining components are closed loops.
        for (const auto key:activeVertices)
        {
            const auto& vertex=vertices[static_cast<size_t>(key)];
            if (vertex.second<0 && !contour.segments[static_cast<size_t>(vertex.first)].used)
                follow(key,vertex.first);
        }
        for (size_t i=0;i<contour.segments.size();++i)
            if (!contour.segments[i].used) follow(contour.segments[i].aKey,static_cast<int>(i));
    }

    void prepareField(int w, int h, double time, float scale)
    {
        if (w==width && h==height && std::abs(time-previousTime)<std::numeric_limits<double>::epsilon()
            && std::abs(scale-previousScale)<std::numeric_limits<float>::epsilon()) return;
        width=w; height=h; previousTime=time; previousScale=scale;
        cols=(w+cellSize-1)/cellSize+1; rows=(h+cellSize-1)/cellSize+1;
        field.resize(static_cast<size_t>(cols*rows));
        const auto noiseScale=.003*static_cast<double>(scale)/2.5;
        const auto sampleRows=[this,noiseScale,time](int firstRow,int lastRow)
        {
            float low=std::numeric_limits<float>::infinity(), high=-low;
            for (int y=firstRow; y<lastRow; ++y)
                for (int x=0; x<cols; ++x)
                {
                    const auto value=static_cast<float>(noise.fbm(x*cellSize*noiseScale,y*cellSize*noiseScale,time*.15));
                    field[static_cast<size_t>(y*cols+x)]=value;
                    low=std::min(low,value); high=std::max(high,value);
                }
            return std::pair<float,float>(low,high);
        };
        std::pair<float,float> limits;
        if (field.size()<8192)
            limits=sampleRows(0,rows);
        else
        {
            // Same samples and normalization, calculated in disjoint rows on shared workers.
            static juce::ThreadPool pool(juce::ThreadPoolOptions{}.withThreadName("Topographic field")
                .withNumberOfThreads(juce::jlimit(1,4,juce::SystemStats::getNumCpus()))
                .withDesiredThreadPriority(juce::Thread::Priority::background));
            const auto parts=pool.getNumThreads();
            std::array<std::pair<float,float>,4> ranges {};
            std::atomic<int> remaining { parts };
            juce::WaitableEvent finished;
            for (int part=0; part<parts; ++part)
                pool.addJob([&,part]
                {
                    ranges[static_cast<size_t>(part)]=sampleRows(rows*part/parts,rows*(part+1)/parts);
                    if (remaining.fetch_sub(1)==1) finished.signal();
                });
            finished.wait();
            limits={std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
            for (int part=0; part<parts; ++part)
            {
                limits.first=std::min(limits.first,ranges[static_cast<size_t>(part)].first);
                limits.second=std::max(limits.second,ranges[static_cast<size_t>(part)].second);
            }
        }
        const auto difference=static_cast<double>(limits.second)-limits.first;
        const auto range=difference>0?difference:1.0;
        for (auto& value:field) value=static_cast<float>((static_cast<double>(value)-limits.first)/range);
        ++fieldGeneration;
    }

    void prepareGeometry(int density,juce::Rectangle<int> clip,int padding)
    {
        // Include the original glow/caps and one device pixel of antialiasing.
        clip=clip.expanded(padding).getIntersection({0,0,width,height});
        const auto firstX=juce::jlimit(0,cols-2,clip.getX()/cellSize);
        const auto firstY=juce::jlimit(0,rows-2,clip.getY()/cellSize);
        const auto lastX=juce::jlimit(firstX,cols-2,clip.getRight()/cellSize);
        const auto lastY=juce::jlimit(firstY,rows-2,clip.getBottom()/cellSize);
        const juce::Rectangle<int> visibleCells(firstX,firstY,lastX-firstX+1,lastY-firstY+1);
        if (geometryGeneration==fieldGeneration && previousDensity==density && geometryCells==visibleCells) return;
        geometryGeneration=fieldGeneration; previousDensity=density; geometryCells=visibleCells;
        const auto refreshLabels=labelGeneration!=fieldGeneration || labelDensity!=density;
        std::vector<std::vector<Label>> candidates(refreshLabels?static_cast<size_t>(density):0);
        contours.resize(static_cast<size_t>(density));
        const juce::Colour amber(200,149,108), gold(212,165,116), coral(224,120,80);
        for (int c=0; c<density; ++c)
        {
            const auto threshold=static_cast<double>(c+1)/(density+1);
            const auto colour=threshold<.5?lerpColour(coral,amber,threshold*2):lerpColour(amber,gold,(threshold-.5)*2);
            const auto alpha=.25+(1-std::abs(threshold-.5)*2)*.45;
            const auto major=c%5==0;
            auto& contour=contours[static_cast<size_t>(c)];
            contour.path.clear(); contour.segments.clear(); contour.colour=colour;
            contour.glowWidth=major?4.5f:2.5f; contour.sharpWidth=major?1.2f:.6f;
            contour.glowAlpha=static_cast<float>(alpha*.25);
            contour.sharpAlpha=static_cast<float>(alpha*(major?1:.8));
        }
        // Visit each cell once and inspect only levels between its corner extrema.
        // Keep the original edge table, interpolation and per-contour segment order.
        const auto beginX=refreshLabels?0:firstX, endX=refreshLabels?cols-2:lastX;
        const auto beginY=refreshLabels?0:firstY, endY=refreshLabels?rows-2:lastY;
        for (int y=beginY; y<=endY; ++y)
            for (int x=beginX; x<=endX; ++x)
            {
                const auto visible=visibleCells.contains(x,y);
                const auto tl=field[static_cast<size_t>(y*cols+x)], tr=field[static_cast<size_t>(y*cols+x+1)];
                const auto br=field[static_cast<size_t>((y+1)*cols+x+1)], bl=field[static_cast<size_t>((y+1)*cols+x)];
                const auto levels=contourRange(tl,tr,br,bl,density);
                for (int c=levels.first; c<=levels.second; ++c)
                {
                    const auto major=c%5==0;
                    if (!visible && (!refreshLabels || !major)) continue;
                    const auto threshold=static_cast<double>(c+1)/(density+1);
                    const auto index=(tl>=threshold?8:0)|(tr>=threshold?4:0)|(br>=threshold?2:0)|(bl>=threshold?1:0);
                    const auto& edges=edgeTable[static_cast<size_t>(index)];
                    auto& contour=contours[static_cast<size_t>(c)];
                    for (int e=0; e<4 && edges[static_cast<size_t>(e)]>=0; e+=2)
                    {
                        const auto label=refreshLabels && major && juce::Random::getSystemRandom().nextDouble()<.003;
                        if (!visible && !label) continue;
                        const auto a=edgePoint(edges[static_cast<size_t>(e)],x*cellSize,y*cellSize,tl,tr,br,bl,threshold);
                        const auto b=edgePoint(edges[static_cast<size_t>(e+1)],x*cellSize,y*cellSize,tl,tr,br,bl,threshold);
                        if (visible) contour.segments.push_back({edgeKey(edges[static_cast<size_t>(e)],x,y),
                            edgeKey(edges[static_cast<size_t>(e+1)],x,y),a,b,false});
                        if (label) candidates[static_cast<size_t>(c)].push_back({(a+b)*.5f,std::atan2(b.y-a.y,b.x-a.x),
                            static_cast<int>(std::lround(threshold*1000)),contour.colour,contour.sharpAlpha*.6f});
                    }
                }
            }
        // Round joins are the same union as round caps on touching segments, without
        // tessellating and rasterizing both overlapping caps at every grid crossing.
        for (auto& contour:contours) connectSegments(contour);
        if (refreshLabels)
        {
            labels.clear();
            for (const auto& level:candidates)
                for (const auto& candidate:level)
                {
                    if (candidate.point.x<80 || candidate.point.x>static_cast<float>(width-80)
                        || candidate.point.y<40 || candidate.point.y>static_cast<float>(height-40)) continue;
                    const auto crowded=std::any_of(labels.begin(),labels.end(),[&candidate](const auto& other)
                    {return candidate.point.getDistanceSquaredFrom(other.point)<120*120;});
                    if (!crowded) labels.push_back(candidate);
                }
            labelGeneration=fieldGeneration; labelDensity=density;
        }
    }
};

TopographicRenderer::TopographicRenderer() : impl(std::make_unique<Impl>(*this)) {}
TopographicRenderer::~TopographicRenderer()
{
    // Join before unique_ptr clears impl: the worker renders through this owner.
    impl->stopThread(-1);
}

void TopographicRenderer::paintPreview(juce::Graphics& g,int width,int height,double time,
                                      float scale,int density,int colour,juce::Rectangle<int> bounds,
                                      float displayScale,float thickness)
{
    if (g.isClipEmpty() || bounds.isEmpty()) return;
    const auto image=impl->requestPreview({width,height,
        juce::jmax(1,juce::roundToInt(static_cast<float>(bounds.getWidth())*displayScale)),
        juce::jmax(1,juce::roundToInt(static_cast<float>(bounds.getHeight())*displayScale)),
        density,colour,time,scale,thickness});
    if (image.isValid())
        g.drawImage(image,bounds.toFloat());
}

void TopographicRenderer::paint(juce::Graphics& g, int width, int height, double time, float scale, int density, int colour,float thickness)
{
    if (g.isClipEmpty()) return;
    const juce::ScopedLock lock(impl->mutex);
    impl->prepareField(width,height,time,scale);
    const auto physicalScale=juce::jmax(.001f,g.getInternalContext().getPhysicalPixelScaleFactor());
    impl->prepareGeometry(density,g.getClipBounds(),static_cast<int>(std::ceil(2.25f*thickness+1.0f/physicalScale)));
    const AnimationColourFilter filter(colour);
    g.setColour(uiBackground); g.fillRect(0,0,width,height);
    for (const auto& contour : impl->contours)
    {
        const auto lineColour=filter.apply(contour.colour);
        g.setColour(lineColour.withAlpha(contour.glowAlpha));
        g.strokePath(contour.path,juce::PathStrokeType(contour.glowWidth*thickness,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        g.setColour(lineColour.withAlpha(contour.sharpAlpha));
        g.strokePath(contour.path,juce::PathStrokeType(contour.sharpWidth*thickness,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    }
    const auto font = makeUiFont();
    for (const auto& label : impl->labels)
    {
        const juce::Graphics::ScopedSaveState saved(g);
        auto angle=label.angle;
        if (angle>juce::MathConstants<float>::halfPi) angle-=juce::MathConstants<float>::pi;
        if (angle<-juce::MathConstants<float>::halfPi) angle+=juce::MathConstants<float>::pi;
        g.addTransform(juce::AffineTransform::rotation(angle).translated(label.point.x,label.point.y));
        const auto text=juce::String(label.elevation);
        const auto textWidth=juce::GlyphArrangement::getStringWidth(font,text)+6;
        g.setColour(uiBackground.withAlpha(.85f)); g.fillRect(-textWidth*.5f,-font.getHeight()*.5f-1.f,textWidth,font.getHeight()+2.f);
        g.setFont(font); g.setColour(filter.apply(label.colour).withAlpha(label.alpha));
        g.drawText(text,juce::Rectangle<float>(-textWidth*.5f,-font.getHeight()*.5f,textWidth,font.getHeight()),juce::Justification::centred,false);
    }
    const auto inner=static_cast<float>(std::min(width,height))*.3f, outer=static_cast<float>(std::max(width,height))*.75f;
    const auto cx=static_cast<float>(width)*.5f, cy=static_cast<float>(height)*.5f;
    juce::ColourGradient vignette(uiBackground.withAlpha(0.f),cx,cy,uiBackground.withAlpha(.4f),cx+outer,cy,true);
    vignette.addColour(inner/outer,uiBackground.withAlpha(0.f));
    g.setGradientFill(vignette); g.fillRect(0,0,width,height);
}
