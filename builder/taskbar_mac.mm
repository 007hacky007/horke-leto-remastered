// Dock tile with a Windows 98 style segmented progress bar over the app icon.
#include "taskbar.h"
#import <AppKit/AppKit.h>

@interface HBDockProgress : NSView
@property double fraction;
@property BOOL failed;
@end

@implementation HBDockProgress
- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    [[NSApp applicationIconImage] drawInRect:b];
    CGFloat w = b.size.width, h = b.size.height;
    NSRect bar = NSMakeRect(w * 0.08, h * 0.06, w * 0.84, h * 0.16);
    // Raised grey frame, sunken white well: the same look as the builder window.
    [[NSColor colorWithCalibratedWhite:0.75 alpha:1] setFill];
    NSRectFill(bar);
    NSRect well = NSInsetRect(bar, w * 0.018, h * 0.018);
    [[NSColor whiteColor] setFill];
    NSRectFill(well);
    [[NSColor colorWithCalibratedWhite:0.5 alpha:1] setFill];
    NSRectFill(NSMakeRect(well.origin.x, NSMaxY(well) - 1, well.size.width, 1));
    NSRectFill(NSMakeRect(well.origin.x, well.origin.y, 1, well.size.height));
    NSColor* block = self.failed ? [NSColor colorWithCalibratedRed:0.75 green:0.05 blue:0.05 alpha:1]
                                 : [NSColor colorWithCalibratedRed:0 green:0 blue:0.5 alpha:1];
    [block setFill];
    NSRect inner = NSInsetRect(well, w * 0.012, h * 0.022);
    const int count = 14;
    CGFloat step = inner.size.width / count, gap = MAX(1.0, step * 0.18);
    int lit = int(count * MAX(0.0, MIN(1.0, self.fraction)) + 0.0001);
    if (self.failed) lit = count;
    for (int i = 0; i < lit; ++i)
        NSRectFill(NSMakeRect(inner.origin.x + i * step, inner.origin.y, step - gap, inner.size.height));
}
@end

namespace hb {
static HBDockProgress* view = nil;
static int shown = -2;  // last drawn permille, -1 cleared

void taskbar_progress(SDL_Window*, TaskbarState state, double fraction) {
    @autoreleasepool {
        NSDockTile* tile = [NSApp dockTile];
        if (state == TaskbarState::None) {
            if (shown == -1) return;
            shown = -1;
            tile.contentView = nil;
            [tile display];
            return;
        }
        int permille = state == TaskbarState::Error ? 2000 : int(fraction * 1000);
        if (permille == shown) return;
        shown = permille;
        if (!view) view = [[HBDockProgress alloc] initWithFrame:NSMakeRect(0, 0, tile.size.width, tile.size.height)];
        view.fraction = fraction;
        view.failed = state == TaskbarState::Error;
        [view setNeedsDisplay:YES];
        tile.contentView = view;
        [tile display];
        if (const char* snapshot = getenv("HB_DOCK_SNAPSHOT")) {  // developer check without screen capture
            NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
            [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(snapshot) atomically:YES];
        }
    }
}

void taskbar_attention(SDL_Window*) {
    @autoreleasepool { [NSApp requestUserAttention:NSInformationalRequest]; }
}
}
