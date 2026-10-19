// Load JUCE GL types before Cocoa, and Cocoa before JuceHeader's namespace imports.
#include <juce_opengl/juce_opengl.h>
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "AuxiliaryWindowFocus.h"
#include "AuxiliaryWindowAttachment.h"

// Auxiliary peers stay visible when the host loses focus. Only their level is
// lowered, so a host's floating FX level cannot leak above other applications.
@interface AVAAuxiliaryActivation : NSObject
{
    NSWindow* auxiliaryWindow;
    NSInteger hostLevel;
}
- (instancetype)initWithWindow:(NSWindow*)window;
- (void)setHostLevel:(NSInteger)level;
- (void)activationChanged:(NSNotification*)notification;
@end

@implementation AVAAuxiliaryActivation
- (instancetype)initWithWindow:(NSWindow*)window
{
    self = [super init];
    if (self != nil)
    {
        auxiliaryWindow = window; // Owned by the associated window, not retained.
        hostLevel = window.level;
        auto* notifications = [NSNotificationCenter defaultCenter];
        [notifications addObserver:self selector:@selector(activationChanged:)
                              name:NSApplicationDidBecomeActiveNotification object:NSApp];
        [notifications addObserver:self selector:@selector(activationChanged:)
                              name:NSApplicationDidResignActiveNotification object:NSApp];
    }
    return self;
}
- (void)setHostLevel:(NSInteger)level
{
    hostLevel = level;
    auxiliaryWindow.level = NSApp.active ? hostLevel : NSNormalWindowLevel;
}
- (void)activationChanged:(NSNotification*)notification
{
    auxiliaryWindow.level = [notification.name isEqualToString:NSApplicationDidBecomeActiveNotification]
        ? hostLevel : NSNormalWindowLevel;
}
- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [super dealloc];
}
@end

namespace
{
char auxiliaryActivationKey;
AVAAuxiliaryActivation* activationFor(NSWindow* window)
{
    auto* activation = static_cast<AVAAuxiliaryActivation*>(objc_getAssociatedObject(window, &auxiliaryActivationKey));
    if (activation == nil)
    {
        activation = [[AVAAuxiliaryActivation alloc] initWithWindow:window];
        objc_setAssociatedObject(window, &auxiliaryActivationKey, activation, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [activation release];
    }
    return activation;
}
NSWindow* nativeWindowFor(juce::Component& component)
{
    auto* view = static_cast<NSView*>(component.getWindowHandle());
    return view != nil ? view.window : nil;
}
}

void auxiliary_window::attachToOwner(juce::Component& component, juce::Component& owner)
{
    auto* window = nativeWindowFor(component);
    auto* ownerWindow = nativeWindowFor(owner);
    if (window == nil || ownerWindow == nil || window == ownerWindow)
        return;

    window.acceptsMouseMovedEvents = YES;
    if (window.parentWindow != ownerWindow)
    {
        if (auto* oldParent = window.parentWindow)
            [oldParent removeChildWindow:window];
        [ownerWindow addChildWindow:window ordered:NSWindowAbove];
    }
    [activationFor(window) setHostLevel:ownerWindow.level];
    window.hidesOnDeactivate = NO;
}

void auxiliary_window::detachFromOwner(juce::Component& component)
{
    if (auto* window = nativeWindowFor(component))
        if (auto* parent = window.parentWindow)
            [parent removeChildWindow:window];
}

void setAuxiliaryTextInputActive(juce::Component& component, const bool active,
                                 void*& previousKeyWindow)
{
    auto* view = static_cast<NSView*>(component.getWindowHandle());
    auto* window = view.window;

    if (active)
    {
        auto* currentKeyWindow = NSApp.keyWindow;
        if (currentKeyWindow != nil && currentKeyWindow != window && previousKeyWindow == nullptr)
            previousKeyWindow = [currentKeyWindow retain];

        component.getProperties().set("mixolveAuxiliaryTextInputActive", true);
    }
    else
    {
        component.getProperties().set("mixolveAuxiliaryTextInputActive", false);

        auto* previous = static_cast<NSWindow*>(previousKeyWindow);
        if (previous != nil)
        {
            if (previous.visible && previous != window)
                [previous makeKeyWindow];
            [previous release];
            previousKeyWindow = nullptr;
        }
    }
}
