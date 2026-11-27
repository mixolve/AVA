#include "Editor.h"
#include "Processor.h"
#include "EditorStateSchema.h"

namespace
{
// The host scales this outer editor for display DPI. User zoom belongs to its
// child, so host scale changes cannot overwrite the chosen interface size.
class BorderlessBottomResizer final : public juce::ResizableEdgeComponent
{
public:
    BorderlessBottomResizer(juce::Component* target, juce::ComponentBoundsConstrainer* constrainer)
        : juce::ResizableEdgeComponent(target, constrainer, juce::ResizableEdgeComponent::bottomEdge) {}

    void paint(juce::Graphics&) override {}
};

class ZoomedEditor final : public juce::AudioProcessorEditor,
                           public DockWindowResizer,
                           private juce::ValueTree::Listener,
                           private juce::AsyncUpdater
{
public:
    explicit ZoomedEditor(AvaAudioProcessor& processorToEdit)
        : AudioProcessorEditor(processorToEdit), state(processorToEdit.getValueTreeState().state), content(processorToEdit)
    {
        setName("zoomed-editor");
        setOpaque(true);
        addAndMakeVisible(content);
        setResizable(true, false);
        resizeHandle = std::make_unique<BorderlessBottomResizer>(this, getConstrainer());
        resizeHandle->setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        addAndMakeVisible(*resizeHandle);
        resizeHandle->toFront(false);
        state.addListener(this);
        applyZoom();
    }

    ~ZoomedEditor() override
    {
        state.removeListener(this);
        cancelPendingUpdate();
    }

    void paint(juce::Graphics& graphics) override { graphics.fillAll(uiPanelBackground); }

    void resizeDockContent(int width, int height) override
    {
        // Prevent childBoundsChanged from forcing an intermediate host resize
        // while the child switches between full editor and compact dock sizes.
        const juce::ScopedValueSetter<bool> guard(synchronisingBounds, true);
        const auto previousWidth = content.getWidth();
        const auto previousHeight = content.getHeight();
        content.setResizeLimits(juce::jmin(previousWidth, width),
                                juce::jmin(previousHeight, height),
                                juce::jmax(previousWidth, width),
                                juce::jmax(previousHeight, height));
        content.setSize(width, height);
        content.setResizeLimits(width, content.getMinimumEditorHeight(),
                                width, content.getMaximumEditorHeight());

        const auto targetWidth = scaled(width);
        const auto targetHeight = scaled(height);
        setResizeLimits(juce::jmin(getWidth(), targetWidth),
                        juce::jmin(getHeight(), targetHeight),
                        juce::jmax(getWidth(), targetWidth),
                        juce::jmax(getHeight(), targetHeight));
        setSize(targetWidth, targetHeight);
        updateResizeLimits();
        updateHandleBounds();
    }

    void resized() override
    {
        updateHandleBounds();
        if (synchronisingBounds || getWidth() <= 0 || getHeight() <= 0) return;
        const juce::ScopedValueSetter<bool> guard(synchronisingBounds, true);
        const auto width = getEditorWidth(content);
        const auto logicalHeight = juce::jlimit(content.getMinimumEditorHeight(), content.getMaximumEditorHeight(),
            juce::roundToInt(getHeight() / zoom));
        if (content.getWidth() != width || content.getHeight() != logicalHeight)
            content.setSize(width, logicalHeight);
        updateResizeLimits();
        const auto physicalWidth = scaled(width);
        const auto physicalHeight = scaled(logicalHeight);
        if (getWidth() != physicalWidth || getHeight() != physicalHeight)
            setSize(physicalWidth, physicalHeight);
        updateHandleBounds();
    }

private:
    void updateHandleBounds()
    {
        if (resizeHandle != nullptr)
        {
            resizeHandle->setVisible(!content.isDockOnly());
            const auto stripHeight = juce::jmax(6, scaled(uiGap));
            resizeHandle->setBounds(0, juce::jmax(0, getHeight() - stripHeight),
                                    getWidth(), stripHeight);
            resizeHandle->toFront(false);
        }
    }

    int scaled(const int pixels) const { return juce::roundToInt(pixels * zoom); }

    void applyZoom()
    {
        const juce::ScopedValueSetter<bool> guard(synchronisingBounds, true);
        zoom = shell_editor_ui::zoomStep(state) / 8.0;
        const auto height = juce::jlimit(content.getMinimumEditorHeight(), content.getMaximumEditorHeight(),
            content.getHeight() > 0 ? content.getHeight() : getEditorHeight(content));
        content.setScaleFactor(static_cast<float>(zoom));
        content.setSize(getEditorWidth(content), height);
        updateResizeLimits();
        setSize(scaled(getEditorWidth(content)), scaled(height));
        updateHandleBounds();
        repaint();
    }

    void updateResizeLimits()
    {
        getConstrainer()->setFixedAspectRatio(0.0);
        const auto width = scaled(getEditorWidth(content));
        setResizeLimits(width, scaled(content.getMinimumEditorHeight()), width, scaled(content.getMaximumEditorHeight()));
    }

    void childBoundsChanged(juce::Component* child) override
    {
        if (child == &content && !synchronisingBounds)
        {
            const juce::ScopedValueSetter<bool> guard(synchronisingBounds, true);
            updateResizeLimits();
            const auto width = scaled(getEditorWidth(content));
            const auto height = scaled(juce::jlimit(content.getMinimumEditorHeight(), content.getMaximumEditorHeight(),
                                                     content.getHeight()));
            if (getWidth() != width || getHeight() != height)
                setSize(width, height);
            updateHandleBounds();
        }
    }

    void handleAsyncUpdate() override { applyZoom(); }
    void valueTreePropertyChanged(juce::ValueTree& changed, const juce::Identifier& property) override
    {
        if (changed == state && (property == shell_editor_ui::key("zoom_step")
                                 || property == shell_editor_ui::key("editor_columns")))
            triggerAsyncUpdate();
    }
    void valueTreeRedirected(juce::ValueTree&) override { triggerAsyncUpdate(); }

    juce::ValueTree& state;
    AvaAudioProcessorEditor content;
    std::unique_ptr<BorderlessBottomResizer> resizeHandle;
    double zoom = 1.0;
    bool synchronisingBounds = false;
};
}

bool AvaAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AvaAudioProcessor::createEditor()
{
    return new ZoomedEditor(*this);
}
