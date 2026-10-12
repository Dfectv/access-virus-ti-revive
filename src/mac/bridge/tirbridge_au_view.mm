// tirbridge_au_view.mm - Cocoa view factory of the Audio Unit (kAudioUnitProperty_CocoaUI): the host asks it for an
// NSView; it hosts the same editor as the VST3 (tirbridge_view.mm: the original's editor mirrored from the helper)
// in a container view, attached while the container is in a window.
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>
#import <Cocoa/Cocoa.h>

#include "tirbridge_au.h"
#include "tirbridge_core.h"

using namespace Steinberg;

@interface TIRAUContainerView : NSView
- (instancetype)initWithCore:(BridgeCore *)core;
@end

@implementation TIRAUContainerView {
    BridgeCore *_core;
    IPlugView *_view;
    bool _attached;
}

- (instancetype)initWithCore:(BridgeCore *)core
{
    _core = core;
    _view = nullptr;
    _attached = false;
    NSRect frame = NSMakeRect(0, 0, 400, 120);
    if (core && core->ok && !core->view) {
        core->hold(); // the instance outlives its view
        _view = bridge_create_view(core);
        core->view = _view;
        ViewRect r{};
        if (_view->getSize(&r) == kResultOk && r.getWidth() > 0)
            frame = NSMakeRect(0, 0, r.getWidth(), r.getHeight());
    }
    self = [super initWithFrame:frame];
    if (self && !_view) { // no editor (instance not running or a second view of the same instance)
        NSTextField *t = [NSTextField labelWithString:@"Virus TI (TI Revive): editor not available"];
        [t setFrame:NSInsetRect(frame, 20, 40)];
        [self addSubview:t];
    }
    return self;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if ([self window] && _view && !_attached) {
        _attached = _view->attached((__bridge void *)self, kPlatformTypeNSView) == kResultOk;
    } else if (![self window] && _attached) {
        _view->removed();
        _attached = false;
    }
}

- (void)dealloc
{
    if (_view) {
        if (_attached)
            _view->removed();
        _view->release(); // clears core->view (editor_view_gone)
        _view = nullptr;
        _core->unhold();
    }
}
@end

@interface TIReviveVTIViewFactory_0_6 : NSObject <AUCocoaUIBase>
@end

@implementation TIReviveVTIViewFactory_0_6
- (unsigned)interfaceVersion
{
    return 0;
}
- (NSString *)description
{
    return @"Virus TI (TI Revive)";
}
- (NSView *)uiViewForAudioUnit:(AudioUnit)au withSize:(NSSize)size
{
    (void)size;
    void *core = nullptr;
    UInt32 sz = sizeof core;
    if (AudioUnitGetProperty(au, kTIRPropertyCore, kAudioUnitScope_Global, 0, &core, &sz) != noErr)
        core = nullptr;
    return [[TIRAUContainerView alloc] initWithCore:(BridgeCore *)core];
}
@end
