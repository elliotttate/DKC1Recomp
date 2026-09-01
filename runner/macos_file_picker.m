#import "macos_file_picker.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <stdlib.h>
#include <string.h>

@interface Dkc1MenuController : NSObject
- (void)runCommand:(id)sender;
@end

static Dkc1MenuController *s_menu_controller;
static NSMenuItem *s_menu_items[kDkc1MacMenuCommandCount];

@interface Dkc1DisplayLinkController : NSObject {
@public
  NSCondition *condition;
  CADisplayLink *displayLink;
  BOOL started;
  BOOL stopRequested;
  BOOL stopped;
  unsigned long long callbackNumber;
  CFTimeInterval timestamp;
  CFTimeInterval targetTimestamp;
  CFTimeInterval duration;
}
- (void)displayLinkFired:(CADisplayLink *)sender;
- (void)runDisplayLinkThread;
@end

static Dkc1DisplayLinkController *s_display_link_controller;

@implementation Dkc1MenuController
- (void)runCommand:(id)sender {
  Dkc1MacMenuCommand((int)[sender tag]);
}
@end

@implementation Dkc1DisplayLinkController

- (void)displayLinkFired:(CADisplayLink *)sender {
  [condition lock];
  callbackNumber++;
  timestamp = sender.timestamp;
  targetTimestamp = sender.targetTimestamp;
  duration = sender.duration;
  [condition signal];
  [condition unlock];
}

- (void)runDisplayLinkThread {
  @autoreleasepool {
    [NSThread currentThread].qualityOfService =
        NSQualityOfServiceUserInteractive;
    NSRunLoop *runLoop = [NSRunLoop currentRunLoop];
    [displayLink addToRunLoop:runLoop forMode:NSDefaultRunLoopMode];
    [condition lock];
    started = YES;
    [condition signal];
    [condition unlock];

    for (;;) {
      [condition lock];
      BOOL shouldStop = stopRequested;
      [condition unlock];
      if (shouldStop)
        break;
      @autoreleasepool {
        [runLoop runMode:NSDefaultRunLoopMode
              beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.050]];
      }
    }

    [displayLink invalidate];
    [condition lock];
    stopped = YES;
    [condition broadcast];
    [condition unlock];
  }
}

@end

static NSMenuItem *AddCommand(NSMenu *menu, NSString *title, int command,
                              NSString *key, NSEventModifierFlags modifiers) {
  NSMenuItem *item = [[NSMenuItem alloc]
      initWithTitle:title
             action:@selector(runCommand:)
      keyEquivalent:key ?: @""];
  item.target = s_menu_controller;
  item.tag = command;
  if (key.length)
    item.keyEquivalentModifierMask = modifiers;
  [menu addItem:item];
  if (command > 0 && command < kDkc1MacMenuCommandCount)
    s_menu_items[command] = item;
  return item;
}

static NSMenuItem *AddSubmenu(NSMenu *parent, NSString *title,
                              NSMenu *submenu) {
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
                                                action:nil
                                         keyEquivalent:@""];
  item.submenu = submenu;
  [parent addItem:item];
  return item;
}

static char *CopyFileSystemPath(NSString *path) {
  const char *file_system_path = path.fileSystemRepresentation;
  if (!file_system_path)
    return NULL;
  size_t size = strlen(file_system_path) + 1;
  char *copy = malloc(size);
  if (copy)
    memcpy(copy, file_system_path, size);
  return copy;
}

static BOOL HasMsuTrackOne(NSString *directory) {
  NSFileManager *manager = [NSFileManager defaultManager];
  NSString *track = [directory stringByAppendingPathComponent:@"track-1.pcm"];
  NSString *legacy =
      [directory stringByAppendingPathComponent:@"dkc_msu-1.pcm"];
  return [manager isReadableFileAtPath:track] ||
         [manager isReadableFileAtPath:legacy];
}

static void ShowMsuError(NSString *message) {
  NSAlert *alert = [[NSAlert alloc] init];
  alert.messageText = @"Unable to use MSU-1 music pack";
  alert.informativeText = message ?: @"Unknown error";
  alert.alertStyle = NSAlertStyleCritical;
  [alert runModal];
  [alert release];
}

static NSString *ExtractMsuArchive(NSURL *archive) {
  NSFileManager *manager = [NSFileManager defaultManager];
  NSURL *applicationSupport =
      [[manager URLsForDirectory:NSApplicationSupportDirectory
                       inDomains:NSUserDomainMask] firstObject];
  if (!applicationSupport)
    return nil;
  NSURL *root = [applicationSupport URLByAppendingPathComponent:@"Flat2VR"
                                                    isDirectory:YES];
  root = [root URLByAppendingPathComponent:@"DKC1Recomp" isDirectory:YES];
  root = [root URLByAppendingPathComponent:@"MSU1" isDirectory:YES];
  NSError *directoryError = nil;
  if (![manager createDirectoryAtURL:root
          withIntermediateDirectories:YES attributes:nil
                               error:&directoryError]) {
    ShowMsuError(directoryError.localizedDescription);
    return nil;
  }

  NSString *name = archive.lastPathComponent.stringByDeletingPathExtension;
  if (!name.length)
    name = @"MusicPack";
  NSURL *destination = [root URLByAppendingPathComponent:name isDirectory:YES];
  if (![manager createDirectoryAtURL:destination
          withIntermediateDirectories:YES attributes:nil
                               error:&directoryError]) {
    ShowMsuError(directoryError.localizedDescription);
    return nil;
  }

  NSTask *task = [[NSTask alloc] init];
  NSPipe *errorPipe = [NSPipe pipe];
  task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/ditto"];
  task.arguments = @[@"-x", @"-k", archive.path, destination.path];
  task.standardError = errorPipe;
  @try {
    [task launch];
    [task waitUntilExit];
  } @catch (NSException *exception) {
    ShowMsuError(exception.reason);
    [task release];
    return nil;
  }
  if (task.terminationStatus != 0) {
    NSData *data = [[errorPipe fileHandleForReading] readDataToEndOfFile];
    NSString *detail = [[[NSString alloc] initWithData:data
                                               encoding:NSUTF8StringEncoding]
        autorelease];
    ShowMsuError(detail.length ? detail : @"The archive extractor failed.");
    [task release];
    return nil;
  }
  [task release];
  if (!HasMsuTrackOne(destination.path)) {
    ShowMsuError(@"The archive does not contain track-1.pcm.");
    return nil;
  }
  return destination.path;
}

void Dkc1MacInstallMenu(void) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    if (s_menu_controller)
      return;
    s_menu_controller = [[Dkc1MenuController alloc] init];

    NSMenu *bar = [[NSMenu alloc] initWithTitle:@""];

    NSMenu *app = [[NSMenu alloc] initWithTitle:@"DKC1Recomp"];
    NSMenuItem *about = [[NSMenuItem alloc]
        initWithTitle:@"About DKC1Recomp"
               action:@selector(orderFrontStandardAboutPanel:)
        keyEquivalent:@""];
    about.target = NSApp;
    [app addItem:about];
    [app addItem:[NSMenuItem separatorItem]];
    NSMenuItem *hide = [[NSMenuItem alloc]
        initWithTitle:@"Hide DKC1Recomp"
               action:@selector(hide:)
        keyEquivalent:@"h"];
    hide.target = NSApp;
    [app addItem:hide];
    NSMenuItem *hide_others = [[NSMenuItem alloc]
        initWithTitle:@"Hide Others"
               action:@selector(hideOtherApplications:)
        keyEquivalent:@"h"];
    hide_others.target = NSApp;
    hide_others.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [app addItem:hide_others];
    NSMenuItem *show_all = [[NSMenuItem alloc]
        initWithTitle:@"Show All"
               action:@selector(unhideAllApplications:)
        keyEquivalent:@""];
    show_all.target = NSApp;
    [app addItem:show_all];
    [app addItem:[NSMenuItem separatorItem]];
    AddCommand(app, @"Quit DKC1Recomp", kDkc1MacMenuQuit, @"q",
               NSEventModifierFlagCommand);
    AddSubmenu(bar, @"DKC1Recomp", app);

    NSMenu *game = [[NSMenu alloc] initWithTitle:@"Game"];
    AddCommand(game, @"Pause", kDkc1MacMenuPause, @"p",
               NSEventModifierFlagCommand);
    AddCommand(game, @"Step One Frame", kDkc1MacMenuStep, @".",
               NSEventModifierFlagCommand);
    [game addItem:[NSMenuItem separatorItem]];
    AddCommand(game, @"Quick Save State", kDkc1MacMenuQuickSave, @"s",
               NSEventModifierFlagCommand);
    AddCommand(game, @"Quick Load State", kDkc1MacMenuQuickLoad, @"l",
               NSEventModifierFlagCommand);
    [game addItem:[NSMenuItem separatorItem]];
    AddCommand(game, @"Export Repro Bundle", kDkc1MacMenuExportRepro, @"",
               0);
    AddSubmenu(bar, @"Game", game);

    NSMenu *music = [[NSMenu alloc] initWithTitle:@"Music"];
    AddCommand(music, @"Choose MSU-1 Music Pack…",
               kDkc1MacMenuChooseMusicPack, @"", 0);
    AddCommand(music, @"Disable Replacement Music",
               kDkc1MacMenuDisableMusicPack, @"", 0);
    AddSubmenu(bar, @"Music", music);

    NSMenu *aspect = [[NSMenu alloc] initWithTitle:@"Aspect Ratio"];
    AddCommand(aspect, @"Native 4:3 (256x224)", kDkc1MacMenuAspectNative,
               @"", 0);
    AddCommand(aspect, @"Widescreen 16:10 (308x224)",
               kDkc1MacMenuAspect16x10, @"", 0);
    AddCommand(aspect, @"Widescreen 16:9 (342x224)",
               kDkc1MacMenuAspect16x9, @"", 0);
    NSMenu *edge = [[NSMenu alloc] initWithTitle:@"Level Edge"];
    AddCommand(edge, @"Reflect Terrain Past the Wall", kDkc1MacMenuEdgeReflect,
               @"", 0);
    AddCommand(edge, @"Black Past the Wall", kDkc1MacMenuEdgeBars, @"", 0);
    AddCommand(edge, @"Shift View Inward at the Wall", kDkc1MacMenuEdgeShift,
               @"", 0);
    AddCommand(edge, @"Glide View Inward at the Wall", kDkc1MacMenuEdgeGlide,
               @"", 0);
    NSMenu *layers = [[NSMenu alloc] initWithTitle:@"Layers"];
    AddCommand(layers, @"Composite", kDkc1MacMenuLayerComposite, @"", 0);
    AddCommand(layers, @"BG1 Only", kDkc1MacMenuLayerBg1, @"", 0);
    AddCommand(layers, @"BG2 Only", kDkc1MacMenuLayerBg2, @"", 0);
    AddCommand(layers, @"BG3 Only", kDkc1MacMenuLayerBg3, @"", 0);
    AddCommand(layers, @"Sprites Only", kDkc1MacMenuLayerObj, @"", 0);

    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    AddCommand(view, @"Enter Full Screen", kDkc1MacMenuFullscreen, @"f",
               NSEventModifierFlagControl | NSEventModifierFlagCommand);
    NSMenu *fullscreenScaling =
        [[NSMenu alloc] initWithTitle:@"Full Screen Scaling"];
    AddCommand(fullscreenScaling, @"Smooth (Linear)",
               kDkc1MacMenuFullscreenSmooth, @"", 0);
    AddCommand(fullscreenScaling, @"Sharp Bilinear",
               kDkc1MacMenuFullscreenSharpBilinear, @"", 0);
    AddCommand(fullscreenScaling, @"Pixel Sharp (Nearest)",
               kDkc1MacMenuFullscreenPixelSharp, @"", 0);
    AddSubmenu(view, @"Full Screen Scaling", fullscreenScaling);
    [view addItem:[NSMenuItem separatorItem]];
    AddSubmenu(view, @"Aspect Ratio", aspect);
    AddSubmenu(view, @"Level Edge", edge);
    AddSubmenu(view, @"Layers", layers);
    [view addItem:[NSMenuItem separatorItem]];
    AddCommand(view, @"Provenance Overlay", kDkc1MacMenuProvenance, @"", 0);
    AddSubmenu(bar, @"View", view);

    NSApp.mainMenu = bar;
  }
}

void Dkc1MacUpdateMenuState(int paused, int fullscreen,
                            Dkc1MacFullscreenScaling fullscreen_scaling,
                            Dkc1VideoAspect aspect, Dkc1EdgePolicy edge,
                            unsigned char layer_mask, int provenance,
                            int replacement_music) {
  if (!s_menu_controller)
    return;
  s_menu_items[kDkc1MacMenuPause].title = paused ? @"Resume" : @"Pause";
  s_menu_items[kDkc1MacMenuPause].state = paused ? NSControlStateValueOn
                                                 : NSControlStateValueOff;
  s_menu_items[kDkc1MacMenuStep].enabled = paused != 0;
  s_menu_items[kDkc1MacMenuFullscreen].title =
      fullscreen ? @"Exit Full Screen" : @"Enter Full Screen";
  s_menu_items[kDkc1MacMenuFullscreen].state =
      fullscreen ? NSControlStateValueOn : NSControlStateValueOff;
  s_menu_items[kDkc1MacMenuFullscreenSmooth].state =
      fullscreen_scaling == kDkc1MacFullscreenSmooth
          ? NSControlStateValueOn : NSControlStateValueOff;
  s_menu_items[kDkc1MacMenuFullscreenSharpBilinear].state =
      fullscreen_scaling == kDkc1MacFullscreenSharpBilinear
          ? NSControlStateValueOn : NSControlStateValueOff;
  s_menu_items[kDkc1MacMenuFullscreenPixelSharp].state =
      fullscreen_scaling == kDkc1MacFullscreenPixelSharp
          ? NSControlStateValueOn : NSControlStateValueOff;
  s_menu_items[kDkc1MacMenuAspectNative].state =
      aspect == kDkc1VideoAspectNative ? NSControlStateValueOn
                                       : NSControlStateValueOff;
  s_menu_items[kDkc1MacMenuAspect16x10].state =
      aspect == kDkc1VideoAspect16x10 ? NSControlStateValueOn
                                      : NSControlStateValueOff;
  s_menu_items[kDkc1MacMenuAspect16x9].state =
      aspect == kDkc1VideoAspect16x9 ? NSControlStateValueOn
                                     : NSControlStateValueOff;
  const int edge_commands[] = {
    kDkc1MacMenuEdgeReflect, kDkc1MacMenuEdgeBars, kDkc1MacMenuEdgeShift,
    kDkc1MacMenuEdgeGlide
  };
  const Dkc1EdgePolicy edge_policies[] = {
    kDkc1EdgeReflect, kDkc1EdgeBars, kDkc1EdgeShift, kDkc1EdgeGlide
  };
  for (int i = 0; i < 4; i++) {
    s_menu_items[edge_commands[i]].state =
        edge == edge_policies[i] ? NSControlStateValueOn
                                 : NSControlStateValueOff;
    s_menu_items[edge_commands[i]].enabled = aspect != kDkc1VideoAspectNative;
  }
  const int layer_commands[] = {
    kDkc1MacMenuLayerComposite, kDkc1MacMenuLayerBg1,
    kDkc1MacMenuLayerBg2, kDkc1MacMenuLayerBg3, kDkc1MacMenuLayerObj
  };
  const unsigned char layer_masks[] = {0xff, 0x01, 0x02, 0x04, 0x10};
  int selected = 0;
  for (int i = 0; i < 5; i++) {
    if (layer_mask == layer_masks[i])
      selected = i;
    s_menu_items[layer_commands[i]].state = NSControlStateValueOff;
  }
  s_menu_items[layer_commands[selected]].state = NSControlStateValueOn;
  s_menu_items[kDkc1MacMenuProvenance].state =
      provenance ? NSControlStateValueOn : NSControlStateValueOff;
  s_menu_items[kDkc1MacMenuChooseMusicPack].state =
      replacement_music ? NSControlStateValueOn : NSControlStateValueOff;
  NSString *configuredMusic = [[NSUserDefaults standardUserDefaults]
      stringForKey:@"DKC1Msu1Directory"];
  s_menu_items[kDkc1MacMenuDisableMusicPack].enabled =
      replacement_music != 0 || configuredMusic.length != 0;
}

int Dkc1MacDisplayLinkStart(void *native_window, double preferred_fps) {
  @autoreleasepool {
    if (s_display_link_controller)
      return 1;
    if (!native_window || preferred_fps <= 0.0)
      return 0;
    if (@available(macOS 14.0, *)) {
      NSWindow *window = (NSWindow *)native_window;
      Dkc1DisplayLinkController *controller =
          [[Dkc1DisplayLinkController alloc] init];
      controller->condition = [[NSCondition alloc] init];
      controller->displayLink =
          [[window displayLinkWithTarget:controller
                                selector:@selector(displayLinkFired:)] retain];
      if (!controller->displayLink) {
        [controller->condition release];
        [controller release];
        return 0;
      }
      const float rate = (float)preferred_fps;
      controller->displayLink.preferredFrameRateRange =
          CAFrameRateRangeMake(rate, rate, rate);
      controller->displayLink.paused = NO;
      s_display_link_controller = controller;
      [NSThread detachNewThreadSelector:@selector(runDisplayLinkThread)
                               toTarget:controller
                             withObject:nil];

      [controller->condition lock];
      NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
      while (!controller->started && !controller->stopped) {
        if (![controller->condition waitUntilDate:deadline])
          break;
      }
      const BOOL running = controller->started && !controller->stopped;
      [controller->condition unlock];
      if (running)
        return 1;
      Dkc1MacDisplayLinkStop();
    }
    return 0;
  }
}

int Dkc1MacDisplayLinkWait(unsigned long long after_callback_number,
                           double timeout_seconds, double *out_timestamp,
                           double *out_target_timestamp, double *out_duration,
                           unsigned long long *out_callback_number) {
  @autoreleasepool {
    Dkc1DisplayLinkController *controller = s_display_link_controller;
    if (!controller || timeout_seconds <= 0.0)
      return 0;
    [controller->condition lock];
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout_seconds];
    while (controller->callbackNumber <= after_callback_number &&
           !controller->stopped) {
      if (![controller->condition waitUntilDate:deadline])
        break;
    }
    const BOOL fired = controller->callbackNumber > after_callback_number;
    if (fired) {
      if (out_timestamp)
        *out_timestamp = controller->timestamp;
      if (out_target_timestamp)
        *out_target_timestamp = controller->targetTimestamp;
      if (out_duration)
        *out_duration = controller->duration;
      if (out_callback_number)
        *out_callback_number = controller->callbackNumber;
    }
    [controller->condition unlock];
    return fired ? 1 : 0;
  }
}

void Dkc1MacDisplayLinkStop(void) {
  @autoreleasepool {
    Dkc1DisplayLinkController *controller = s_display_link_controller;
    if (!controller)
      return;
    [controller->condition lock];
    controller->stopRequested = YES;
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
    while (!controller->stopped) {
      if (![controller->condition waitUntilDate:deadline])
        break;
    }
    [controller->condition unlock];
    [controller->displayLink release];
    [controller->condition release];
    [controller release];
    s_display_link_controller = nil;
  }
}

char *Dkc1MacChooseRom(void) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp activateIgnoringOtherApps:YES];

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Choose your Donkey Kong Country ROM";
    panel.message = @"DKC1Recomp requires a headerless USA v1.0 .sfc ROM.";
    panel.prompt = @"Open";
    panel.canChooseDirectories = NO;
    panel.canChooseFiles = YES;
    panel.allowsMultipleSelection = NO;
    panel.allowedContentTypes = @[
      [UTType typeWithFilenameExtension:@"sfc"],
      [UTType typeWithFilenameExtension:@"smc"]
    ];

    if ([panel runModal] != NSModalResponseOK)
      return NULL;
    const char *path = panel.URL.fileSystemRepresentation;
    if (!path)
      return NULL;
    size_t size = strlen(path) + 1;
    char *copy = malloc(size);
    if (copy)
      memcpy(copy, path, size);
    return copy;
  }
}

char *Dkc1MacSavedMsu1(void) {
  @autoreleasepool {
    NSString *path = [[NSUserDefaults standardUserDefaults]
        stringForKey:@"DKC1Msu1Directory"];
    if (!path.length || !HasMsuTrackOne(path))
      return NULL;
    return CopyFileSystemPath(path);
  }
}

char *Dkc1MacChooseMsu1(void) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp activateIgnoringOtherApps:YES];

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Choose an MSU-1 music pack";
    panel.message = @"Select a .msu1 archive or an extracted PCM folder.";
    panel.prompt = @"Use Music Pack";
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = YES;
    panel.allowsMultipleSelection = NO;
    panel.allowedContentTypes = @[
      [UTType typeWithFilenameExtension:@"msu1"]
    ];
    if ([panel runModal] != NSModalResponseOK)
      return NULL;

    NSURL *selection = panel.URL;
    NSNumber *isDirectory = nil;
    [selection getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey
                           error:nil];
    NSString *directory = nil;
    if (isDirectory.boolValue) {
      if (!HasMsuTrackOne(selection.path)) {
        ShowMsuError(@"The selected folder does not contain track-1.pcm.");
        return NULL;
      }
      directory = selection.path;
    } else {
      directory = ExtractMsuArchive(selection);
    }
    if (!directory.length)
      return NULL;

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:directory forKey:@"DKC1Msu1Directory"];
    [defaults synchronize];
    return CopyFileSystemPath(directory);
  }
}

void Dkc1MacClearMsu1(void) {
  @autoreleasepool {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults removeObjectForKey:@"DKC1Msu1Directory"];
    [defaults synchronize];
  }
}

Dkc1MacFullscreenScaling Dkc1MacSavedFullscreenScaling(void) {
  @autoreleasepool {
    NSInteger saved = [[NSUserDefaults standardUserDefaults]
        integerForKey:@"DKC1FullscreenScaling"];
    if (saved >= kDkc1MacFullscreenSmooth &&
        saved < kDkc1MacFullscreenScalingCount &&
        [[NSUserDefaults standardUserDefaults]
            objectForKey:@"DKC1FullscreenScaling"] != nil)
      return (Dkc1MacFullscreenScaling)saved;
    return kDkc1MacFullscreenSharpBilinear;
  }
}

Dkc1EdgePolicy Dkc1MacSavedWidescreenEdge(void) {
  @autoreleasepool {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSInteger saved = [defaults integerForKey:@"DKC1WidescreenEdge"];
    if ([defaults objectForKey:@"DKC1WidescreenEdge"] != nil &&
        saved >= kDkc1EdgeReflect && saved < kDkc1EdgePolicyCount)
      return (Dkc1EdgePolicy)saved;
    return kDkc1EdgeGlide;
  }
}

void Dkc1MacSetWidescreenEdge(Dkc1EdgePolicy policy) {
  @autoreleasepool {
    if (policy < kDkc1EdgeReflect || policy >= kDkc1EdgePolicyCount)
      policy = kDkc1EdgeGlide;
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:policy forKey:@"DKC1WidescreenEdge"];
    [defaults synchronize];
  }
}

void Dkc1MacSetFullscreenScaling(Dkc1MacFullscreenScaling scaling) {
  @autoreleasepool {
    if (scaling < kDkc1MacFullscreenSmooth ||
        scaling >= kDkc1MacFullscreenScalingCount)
      scaling = kDkc1MacFullscreenSharpBilinear;
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:scaling forKey:@"DKC1FullscreenScaling"];
    [defaults synchronize];
  }
}
