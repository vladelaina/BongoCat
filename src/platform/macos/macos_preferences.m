#include "bongo_cat/platform.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>

BongoCatResult bongo_cat_platform_set_autostart(bool enabled,
    BongoCatError *error) {
    @autoreleasepool {
        NSString *directory = [NSHomeDirectory()
            stringByAppendingPathComponent:@"Library/LaunchAgents"];
        NSString *path = [directory
            stringByAppendingPathComponent:@"com.bongocat.desktop.plist"];
        NSFileManager *files = [NSFileManager defaultManager];
        if (!enabled) {
            if (![files fileExistsAtPath:path] ||
                [files removeItemAtPath:path error:nil]) return BONGO_CAT_OK;
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot remove macOS launch agent");
            return BONGO_CAT_ERROR_IO;
        }
        NSString *executable = [[NSBundle mainBundle] executablePath];
        if (!executable || ![files createDirectoryAtPath:directory
            withIntermediateDirectories:YES attributes:nil error:nil]) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot create macOS launch agent directory");
            return BONGO_CAT_ERROR_IO;
        }
        NSDictionary *plist = @{ @"Label": @"com.bongocat.desktop",
            @"ProgramArguments": @[executable, @"--autostart"],
            @"RunAtLoad": @YES };
        if ([plist writeToFile:path atomically:YES]) return BONGO_CAT_OK;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot write macOS launch agent");
        return BONGO_CAT_ERROR_IO;
    }
}

void bongo_cat_platform_configure_preferences_window(SDL_Window *sdl_window) {
    if (!sdl_window) return;
    NSWindow *window = (__bridge NSWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(sdl_window),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!window) return;
    NSWindowStyleMask style = [window styleMask];
    style |= NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
        NSWindowStyleMaskFullSizeContentView;
    [window setStyleMask:style];
    [window setTitle:@""];
    [window setTitleVisibility:NSWindowTitleHidden];
    [window setTitlebarAppearsTransparent:YES];
    [window setToolbar:nil];
    [window setTabbingMode:NSWindowTabbingModeDisallowed];
    [window setMovableByWindowBackground:NO];
    [[window standardWindowButton:NSWindowCloseButton] setHidden:NO];
    [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
    [[window standardWindowButton:NSWindowZoomButton] setHidden:NO];
}
#endif
