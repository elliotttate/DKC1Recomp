#import "macos_file_picker.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <stdlib.h>
#include <string.h>

@interface Dkc1MenuController : NSObject
- (void)runCommand:(id)sender;
@end

static Dkc1MenuController *s_menu_controller;
static NSMenuItem *s_menu_items[kDkc1MacMenuCommandCount];

@implementation Dkc1MenuController
- (void)runCommand:(id)sender {
  Dkc1MacMenuCommand((int)[sender tag]);
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

    NSMenu *aspect = [[NSMenu alloc] initWithTitle:@"Aspect Ratio"];
    AddCommand(aspect, @"Native 4:3 (256x224)", kDkc1MacMenuAspectNative,
               @"", 0);
    AddCommand(aspect, @"Widescreen 16:9 (342x224)",
               kDkc1MacMenuAspectWidescreen, @"", 0);
    NSMenu *layers = [[NSMenu alloc] initWithTitle:@"Layers"];
    AddCommand(layers, @"Composite", kDkc1MacMenuLayerComposite, @"", 0);
    AddCommand(layers, @"BG1 Only", kDkc1MacMenuLayerBg1, @"", 0);
    AddCommand(layers, @"BG2 Only", kDkc1MacMenuLayerBg2, @"", 0);
    AddCommand(layers, @"BG3 Only", kDkc1MacMenuLayerBg3, @"", 0);
    AddCommand(layers, @"Sprites Only", kDkc1MacMenuLayerObj, @"", 0);

    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    AddCommand(view, @"Enter Full Screen", kDkc1MacMenuFullscreen, @"f",
               NSEventModifierFlagControl | NSEventModifierFlagCommand);
    [view addItem:[NSMenuItem separatorItem]];
    AddSubmenu(view, @"Aspect Ratio", aspect);
    AddSubmenu(view, @"Layers", layers);
    [view addItem:[NSMenuItem separatorItem]];
    AddCommand(view, @"Provenance Overlay", kDkc1MacMenuProvenance, @"", 0);
    AddSubmenu(bar, @"View", view);

    NSApp.mainMenu = bar;
  }
}

void Dkc1MacUpdateMenuState(int paused, int fullscreen, int widescreen,
                            unsigned char layer_mask, int provenance) {
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
  s_menu_items[kDkc1MacMenuAspectNative].state =
      widescreen ? NSControlStateValueOff : NSControlStateValueOn;
  s_menu_items[kDkc1MacMenuAspectWidescreen].state =
      widescreen ? NSControlStateValueOn : NSControlStateValueOff;
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
