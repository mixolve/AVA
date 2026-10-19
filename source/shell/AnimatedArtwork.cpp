#include "Controls.h"
#include "BurningFilmRenderer.h"
#include "BurningFilmPreview.h"
#include "TopographicRenderer.h"
#include "AnimationColour.h"
#include <juce_opengl/juce_opengl.h>

struct BurningFilmArtwork::Animation
{
    std::shared_ptr<AnimationSettings> settings = std::make_shared<AnimationSettings>();
    bool bottomAnimation = false;
    bool gpuRendering = false;
    int lastSelection = -1;
    juce::String lastHostName;
    bool lastMoving = true;
    double nameScrollPosition = 0.0;
    std::atomic<double> nameScrollStart { juce::Time::getMillisecondCounterHiRes() };
    juce::OpenGLGraphicsContextCustomShader shader { burningFilmFragmentShader };
    mutable TopographicRenderer topographic;
    std::unique_ptr<BurningFilmPreview> preview;
    double lastTick = juce::Time::getMillisecondCounterHiRes();
    std::atomic<double> time { juce::Random::getSystemRandom().nextDouble() * 80.0 };

    std::atomic<double> topographicTime { time.load() };

    void update()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto elapsed = juce::jlimit(0.0, 0.05, (now - lastTick) / 1000.0);
        time.store(time.load() + elapsed);
        const auto& band = settings->band(bottomAnimation);
        if (band.animation.load() == 2)
            topographicTime.store(topographicTime.load() + elapsed * band.modes[1].speed.load());
        lastTick = now;
    }
};

BurningFilmArtwork::BurningFilmArtwork() : animation(std::make_unique<Animation>())
{
    setInterceptsMouseClicks(true, false);
    startTimerHz(30);
}

BurningFilmArtwork::~BurningFilmArtwork() { stopTimer(); }
void BurningFilmArtwork::resetNameScroll()
{
    animation->nameScrollStart.store(juce::Time::getMillisecondCounterHiRes());
    animation->nameScrollPosition = 0.0;
    repaint();
}
void BurningFilmArtwork::resized() { resetNameScroll(); }

void BurningFilmArtwork::timerCallback()
{
    if (! isShowing())
    {
        animation->lastTick = juce::Time::getMillisecondCounterHiRes();
        return;
    }
    const auto* source = animationSource != nullptr ? animationSource.getComponent() : this;
    const auto& band = source->animation->settings->band(source->animation->bottomAnimation);
    const auto selection = band.animation.load();
    const auto selectionChanged = animation->lastSelection != selection;
    animation->lastSelection = selection;
    if (selection == 0 || selection >= 3)
    {
        const auto name = selection == customTextAnimationMode ? band.getCustomText()
            : source->animation->settings->getHostName(selection);
        const auto moving = band.current().moving.load();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (selectionChanged || name != animation->lastHostName || moving != animation->lastMoving)
            resetNameScroll();
        else if (moving && now > animation->nameScrollStart.load() + 1000.0)
            animation->nameScrollPosition += juce::jlimit(0.0, 0.05,
                (now - juce::jmax(animation->lastTick, animation->nameScrollStart.load() + 1000.0)) * 0.001)
                * band.current().speed.load();
        animation->lastMoving = moving;
        if (selection >= 3) repaint();
        animation->lastHostName = name;
        animation->lastTick = juce::Time::getMillisecondCounterHiRes();
        return;
    }
    if (animationSource == nullptr)
        animation->update();
    repaint();
}

void BurningFilmArtwork::paint(juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const auto* source = animationSource != nullptr ? animationSource.getComponent() : this;
    const auto& current = *source->animation;
    const auto& selectedBand = current.settings->band(current.bottomAnimation);
    const auto selection = selectedBand.animation.load();
    if (selection == 0)
    {
        g.setColour(uiGreyLight);
        g.drawRect(getLocalBounds(), 1);
        return;
    }
    g.fillAll(uiBackground);
    if (selection >= 3)
    {
        const auto colour = juce::jlimit(0, static_cast<int>(animationColours.size()) - 1, selectedBand.current().colour.load());
        g.setColour(juce::Colour(0xff000000u | animationColours[static_cast<size_t>(colour)]));
        g.setFont(makeUiFont());
        const auto text = selection == customTextAnimationMode ? selectedBand.getCustomText()
            : current.settings->getHostName(selection);
        const auto elapsed = selectedBand.current().moving.load() ? animation->nameScrollPosition : 0.0;
        drawLoopingText(g, text.isEmpty() ? juce::String::charToString(0x2014) : text,
            getLocalBounds().reduced(uiGap, 1), makeUiFont(), juce::Justification::centred, elapsed);
        g.setColour(uiGreyLight);
        g.drawRect(getLocalBounds(), 1);
        return;
    }
    const auto& band = selectedBand.modes[selection == 2 ? 1 : 0];
    const auto synchronized = current.bottomAnimation && current.settings->same.load()
        && current.settings->sync.load() && source->synchronizationSource != nullptr;
    const auto* geometrySource = synchronized ? source->synchronizationSource.getComponent() : source;
    // Both the strip and its preview use the main editor's width, not preview width.
    const auto editorWidth = geometrySource->getParentComponent() != nullptr
        ? geometrySource->getParentComponent()->getWidth() : initialEditorWidth;
    const auto widthFactor = static_cast<float>(juce::jmax(1, editorWidth))
        / static_cast<float>(initialEditorWidth);
    const auto effectiveScale = 2.5f * widthFactor * band.scaleFactor.load();
    // The strip is a crop of a complete canvas, rather than a vertically squeezed scene.
    const auto canvasSide = juce::jmax(320, geometrySource->getWidth());
    const auto coordinateScale = static_cast<float>(canvasSide) / static_cast<float>(getWidth());
    const auto cropHeight = juce::jmin(static_cast<float>(canvasSide),
        static_cast<float>(geometrySource->getHeight()) * static_cast<float>(canvasSide)
            / static_cast<float>(juce::jmax(1, geometrySource->getWidth())));
    const auto cropOffset = animationSource != nullptr ? 0.0f
        : (static_cast<float>(canvasSide) - cropHeight) * band.cropY.load();
    const auto* clock = &current;
    if (synchronized)
        clock = source->synchronizationSource->animation.get();
    const auto timeSnapshot=clock->time.load();
    const auto topographicSnapshot=clock->topographicTime.load();

    if (selection == 2)
    {
        if (animationSource != nullptr)
        {
            animation->topographic.paintPreview(g,canvasSide,canvasSide,topographicSnapshot,
                effectiveScale,juce::roundToInt(band.density.load()),band.colour.load(),getLocalBounds(),
                g.getInternalContext().getPhysicalPixelScaleFactor(),band.thickness.load());
        }
        else
        {
            const juce::Graphics::ScopedSaveState saved(g);
            g.addTransform(juce::AffineTransform::scale(1.0f / coordinateScale)
                .translated(0.0f, -cropOffset / coordinateScale));
            clock->topographic.paint(g,canvasSide,canvasSide,topographicSnapshot,
                effectiveScale,juce::roundToInt(band.density.load()),band.colour.load(),band.thickness.load());
        }
    }
    else if (animation->preview != nullptr)
    {
        const auto renderingScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        auto image = animation->preview->requestFrame(
            { getLocalBounds(), renderingScale, {}, canvasSide, coordinateScale, cropOffset,
              AnimationColourFilter(band.colour.load()), static_cast<float>(timeSnapshot),
              juce::jmax(.001f, band.speed.load() * 2.f), band.glow.load(), effectiveScale });
        if (image.isValid())
            g.drawImage(image, getLocalBounds().toFloat());
    }
    else if (animation->gpuRendering && juce::OpenGLContext::getCurrentContext() != nullptr)
    {
        auto* context = juce::OpenGLContext::getCurrentContext();
        const auto renderingScale = static_cast<float>(context->getRenderingScale());
        const auto origin = context->getTargetComponent()->getLocalPoint(this, juce::Point<int>());
        const AnimationColourFilter filter(band.colour.load());
        const auto time = static_cast<float>(timeSnapshot);
        const auto burnSpeed = juce::jmax(.001f, band.speed.load() * 2.0f);
        const auto glow = band.glow.load(), patternScale = effectiveScale;
        paintBurningFilmFrame(g, animation->shader,
            { getLocalBounds(), renderingScale, origin, canvasSide, coordinateScale, cropOffset,
              filter, time, burnSpeed, glow, patternScale });
    }
    g.setColour(uiGreyLight);
    g.drawRect(getLocalBounds(), 1);
    if (animationSource != nullptr)
    {
        g.setColour(uiWhite);
        g.drawRect(getCropBounds().reduced(.75f),1.5f);
    }
}

void BurningFilmArtwork::setGpuRenderingEnabled(bool enabled) { animation->gpuRendering = enabled; repaint(); }
void BurningFilmArtwork::setAnimationSettings(std::shared_ptr<AnimationSettings> settings)
{
    animation->settings = std::move(settings);
    repaint();
}
void BurningFilmArtwork::mouseDown(const juce::MouseEvent& event)
{
    const auto* source = animationSource != nullptr ? animationSource.getComponent() : this;
    if (event.mods.isLeftButtonDown() && !event.mods.isPopupMenu()
        && source->animation->settings->band(source->animation->bottomAnimation).animation.load() >= 3)
    {
        resetNameScroll();
        return;
    }
    cropDragActive = animationSource != nullptr && event.mods.isLeftButtonDown()
        && getCropBounds().expanded(0.0f, 3.0f).contains(event.position);
    if (cropDragActive)
    {
        const auto& current = *animationSource->animation;
        const auto& band = (current.settings->band(current.bottomAnimation)).current();
        cropDragStartY = event.position.y;
        cropDragStartPosition = band.cropY.load();
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (event.mods.isPopupMenu() && onSettingsRequested)
        onSettingsRequested();
}
juce::Rectangle<float> BurningFilmArtwork::getCropBounds() const
{
    if (animationSource == nullptr || animationSource->getWidth() <= 0)
        return {};
    const auto& current=*animationSource->animation;
    const auto& selectedBand=current.settings->band(current.bottomAnimation);
    if (selectedBand.animation.load() == 0 || selectedBand.animation.load() >= 3) return {};
    const auto& band=selectedBand.current();
    const auto height=juce::jmin(static_cast<float>(getHeight()),
        static_cast<float>(animationSource->getHeight())*static_cast<float>(getWidth())
            /static_cast<float>(animationSource->getWidth()));
    return { 0.f,(static_cast<float>(getHeight())-height)*band.cropY.load(),static_cast<float>(getWidth()),height };
}
void BurningFilmArtwork::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(animationSource!=nullptr && getCropBounds().expanded(0.f,3.f).contains(event.position)
        ?juce::MouseCursor::DraggingHandCursor:juce::MouseCursor::NormalCursor);
}
void BurningFilmArtwork::mouseDrag(const juce::MouseEvent& event)
{
    if (cropDragActive && animationSource!=nullptr)
    {
        const auto travel=static_cast<float>(getHeight())-getCropBounds().getHeight();
        if (travel<=0.f) return;
        auto& current=*animationSource->animation;
        auto& band=(current.settings->band(current.bottomAnimation)).current();
        const auto position=juce::jlimit(0.f,1.f,cropDragStartPosition+(event.position.y-cropDragStartY)/travel);
        band.cropY.store(position);
        if (onCropChanged) onCropChanged(position);
        animationSource->repaint(); repaint();
        return;
    }
}
void BurningFilmArtwork::mouseUp(const juce::MouseEvent&)
{
    cropDragActive=false;
}
void BurningFilmArtwork::setBottomAnimation(bool bottom) { animation->bottomAnimation = bottom; repaint(); }
void BurningFilmArtwork::ensurePreview()
{
    if (animation->preview != nullptr)
        return;

    animation->preview = std::make_unique<BurningFilmPreview>();
    // This context only renders offscreen frames; it never covers UI content.
    animation->preview->setBounds(-16384, -16384, 1, 1);
    addAndMakeVisible(*animation->preview);
    animation->preview->start();
}
void BurningFilmArtwork::setPreviewEnabled(const bool enabled)
{
    if (enabled)
        ensurePreview();

    repaint();
}
void BurningFilmArtwork::setAnimationSource(BurningFilmArtwork& source)
{
    animationSource = &source;
    ensurePreview();
    repaint();
}

void BurningFilmArtwork::setSynchronizationSource(BurningFilmArtwork& source) { synchronizationSource = &source; }
