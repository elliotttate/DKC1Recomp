#import "macos_pause_menu.h"
#import "macos_file_picker.h"
#import "macos_controls.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define FIELD(n) {#n, offsetof(Dkc1GraphicsSettings,n)}
static const struct { const char *name; size_t offset; } kFields[]={
  FIELD(display),FIELD(upscaler),FIELD(screen),FIELD(reconstruct_mode),FIELD(strength),
  FIELD(softness),FIELD(shading),FIELD(crt.preset),FIELD(crt.scanlines),FIELD(crt.sharpness),
  FIELD(crt.mask),FIELD(crt.mask_strength),FIELD(crt.glow),FIELD(crt.halation),FIELD(crt.curvature),
  FIELD(window_scale),FIELD(fullscreen),FIELD(aspect),FIELD(edge),FIELD(audio_enabled),
  FIELD(volume),FIELD(state_slot),FIELD(hd_polish),FIELD(hd_finish)
};
#undef FIELD
void Dkc1MacSaveGraphics(const Dkc1GraphicsSettings *s) {
  NSMutableDictionary *d=[NSMutableDictionary dictionary];
  for (size_t i=0;i<sizeof kFields/sizeof *kFields;i++)
    d[@(kFields[i].name)]=@(*(const int *)((const char *)s+kFields[i].offset));
  [[NSUserDefaults standardUserDefaults] setObject:d forKey:@"GraphicsV1"];
}
void Dkc1MacLoadGraphics(Dkc1GraphicsSettings *s) {
  Dkc1GraphicsDefault(s); s->edge=Dkc1MacSavedWidescreenEdge();
  NSDictionary *d=[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GraphicsV1"];
  for (size_t i=0;i<sizeof kFields/sizeof *kFields;i++) {
    id n=d[@(kFields[i].name)];
    if ([n isKindOfClass:[NSNumber class]]) *(int *)((char *)s+kFields[i].offset)=[n intValue];
  }
  const char *v=getenv("DKC1_DISPLAY");
  if (v) Dkc1CrtDisplayFromName(v,&s->display);
  v=getenv("DKC1_CRT_PRESET"); int preset;
  if (v && Dkc1CrtPresetFromName(v,&preset)) Dkc1CrtSettingsApplyPreset(&s->crt,preset);
  const char *names[]={"nearest","bilinear","reconstruct","sharp-bilinear"};
  v=getenv("DKC1_UPSCALER");
  if (v) for (int i=0;i<4;i++) if (!strcmp(v,names[i])) s->upscaler=i;
  const char *colors[]={"raw","crt","composite","trinitron"};
  v=getenv("DKC1_SCREEN");
  if (v) for (int i=0;i<4;i++) if (!strcmp(v,colors[i])) s->screen=i;
  const char *keys[]={"DKC1_RECONSTRUCT_MODE","DKC1_RECONSTRUCT_STRENGTH","DKC1_RECONSTRUCT_SOFTNESS","DKC1_RECONSTRUCT_SHADING"};
  int *values[]={&s->reconstruct_mode,&s->strength,&s->softness,&s->shading};
  for (int i=0;i<4;i++) if ((v=getenv(keys[i]))) *values[i]=atoi(v);
  if(!d[@"hd_polish"] && (v=getenv("DKC1_HD_POLISH_DEFAULT")))s->hd_polish=atoi(v);
  if((v=getenv("DKC1_HD_POLISH")))s->hd_polish=atoi(v);
  if(!d[@"hd_finish"] && (v=getenv("DKC1_HD_FINISH_DEFAULT")))s->hd_finish=atoi(v);
  if((v=getenv("DKC1_HD_FINISH")))s->hd_finish=atoi(v);
  Dkc1GraphicsClamp(s);
}

@interface Dkc1MenuDocument : NSView
@end
@implementation Dkc1MenuDocument
- (BOOL)isFlipped { return YES; }
@end
@interface Dkc1PausePanel : NSPanel
@end
@implementation Dkc1PausePanel
- (void)cancelOperation:(id)sender { (void)sender; [NSApp stopModal]; }
- (BOOL)canBecomeKeyWindow { return YES; }
@end

@interface Dkc1PauseMenu : NSObject <NSWindowDelegate> {
@public
  Dkc1PausePanel *panel;
  NSTabView *tabs;
  Dkc1GraphicsSettings draft;
  Dkc1Controls *controls;
  NSMutableDictionary *widgets, *values;
  NSTextField *status;
  NSButton *assistToggle;
  unsigned lastPad;
  BOOL resumeGame;
  NSTimer *timer;
}
- (void)changed:(id)sender;
- (void)action:(id)sender;
- (void)refresh;
@end
static BOOL s_open;
int Dkc1MacPauseMenuIsOpen(void) { return s_open; }

@implementation Dkc1PauseMenu
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender; [NSApp stopModal]; return NO; }
- (void)refresh {
  for (NSString *name in widgets) {
    NSControl *w=widgets[name]; int v=0;
    for (size_t i=0;i<sizeof kFields/sizeof *kFields;i++)
      if ([name isEqualToString:@(kFields[i].name)]) v=*(int *)((char *)&draft+kFields[i].offset);
    if ([w isKindOfClass:[NSPopUpButton class]]) [(NSPopUpButton *)w selectItemAtIndex:v];
    else w.integerValue=v;
    if (values[name]) [values[name] setStringValue:[NSString stringWithFormat:@"%d%@",v,[name isEqualToString:@"window_scale"] ? @"×" : @"%"]];
    BOOL crt=[name hasPrefix:@"crt."];
    BOOL reconstruct=[name hasPrefix:@"reconstruct_"] || [@[@"strength",@"softness",@"shading"] containsObject:name];
    w.enabled=crt ? draft.display==kDkc1DisplayCrt : reconstruct ? draft.display==0 && draft.upscaler==2 :
      [name isEqualToString:@"upscaler"] ? draft.display==0 : YES;
  }
  assistToggle.state=controls->assist_enabled;
  status.stringValue=@(Dkc1MacHostStatus());
}
- (void)changed:(id)sender {
  NSControl *w=sender; NSString *name=w.identifier;
  int v=[w isKindOfClass:[NSPopUpButton class]] ? (int)[(NSPopUpButton *)w indexOfSelectedItem] : w.intValue;
  for (size_t i=0;i<sizeof kFields/sizeof *kFields;i++)
    if ([name isEqualToString:@(kFields[i].name)]) *(int *)((char *)&draft+kFields[i].offset)=v;
  if ([name isEqualToString:@"crt.preset"]) Dkc1CrtSettingsApplyPreset(&draft.crt,v);
  else if ([name hasPrefix:@"crt."]) draft.crt.preset=kDkc1CrtPresetCustom;
  Dkc1GraphicsClamp(&draft); Dkc1MacApplyGraphics(&draft); [panel makeKeyAndOrderFront:nil]; [self refresh];
}
- (void)action:(id)sender {
  NSInteger tag=[sender tag];
  if (tag==0) { resumeGame=YES; [NSApp stopModal]; return; }
  if (tag==1000) {
    Dkc1GraphicsDefault(&draft); Dkc1MacApplyGraphics(&draft); [panel makeKeyAndOrderFront:nil]; [self refresh]; return;
  }
  if (tag==1001) { Dkc1MacAssistEnabled([sender state]==NSControlStateValueOn); return; }
  Dkc1MacMenuCommand((int)tag);
  if (tag==kDkc1MacMenuQuit) [NSApp stopModal];
  [self refresh];
}
- (NSView *)page:(NSString *)title {
  NSTabViewItem *item=[[[NSTabViewItem alloc] initWithIdentifier:title] autorelease]; item.label=title;
  NSScrollView *scroll=[[[NSScrollView alloc] initWithFrame:NSMakeRect(0,0,640,450)] autorelease];
  scroll.hasVerticalScroller=YES; scroll.drawsBackground=NO; scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
  NSView *doc=[[[Dkc1MenuDocument alloc] initWithFrame:NSMakeRect(0,0,620,450)] autorelease];
  doc.autoresizingMask=NSViewWidthSizable; scroll.documentView=doc; item.view=scroll; [tabs addTabViewItem:item]; return doc;
}
- (void)text:(NSString *)text view:(NSView *)view y:(CGFloat *)y height:(CGFloat)height {
  NSTextField *label=[NSTextField wrappingLabelWithString:text];
  label.frame=NSMakeRect(12,*y,view.bounds.size.width-24,height); label.autoresizingMask=NSViewWidthSizable;
  [view addSubview:label]; *y+=height+12;
}
- (void)heading:(NSString *)text view:(NSView *)view y:(CGFloat *)y {
  NSTextField *label=[NSTextField labelWithString:text]; label.font=[NSFont boldSystemFontOfSize:15];
  label.frame=NSMakeRect(12,*y,500,24); [view addSubview:label]; *y+=34;
}
- (void)option:(NSString *)name title:(NSString *)title labels:(NSArray *)labels
          view:(NSView *)view y:(CGFloat *)y max:(int)max {
  CGFloat width=view.bounds.size.width;
  NSTextField *label=[NSTextField labelWithString:title];label.frame=NSMakeRect(12,*y+3,170,24);[view addSubview:label];
  NSControl *w;
  if (labels) {
    NSPopUpButton *p=[[[NSPopUpButton alloc] initWithFrame:NSMakeRect(188,*y,width-204,28) pullsDown:NO] autorelease];
    [p addItemsWithTitles:labels]; w=p;
  } else {
    NSSlider *s=[NSSlider sliderWithValue:0 minValue:[name isEqualToString:@"window_scale"] ? 1 : 0 maxValue:max target:self action:@selector(changed:)];
    s.frame=NSMakeRect(188,*y,width-258,26); s.continuous=YES; w=s;
    NSTextField *value=[NSTextField labelWithString:@""]; value.frame=NSMakeRect(width-65,*y+3,55,22);
    value.autoresizingMask=NSViewMinXMargin; [view addSubview:value]; values[name]=value;
  }
  w.identifier=name; w.target=self; w.action=@selector(changed:); w.autoresizingMask=NSViewWidthSizable;
  [view addSubview:w]; widgets[name]=w; *y+=37;
}
- (void)button:(NSString *)title tag:(int)tag view:(NSView *)view y:(CGFloat *)y {
  NSButton *button=[NSButton buttonWithTitle:title target:self action:@selector(action:)];
  button.frame=NSMakeRect(12,*y,MIN(360,view.bounds.size.width-24),34);button.tag=tag;
  button.autoresizingMask=NSViewWidthSizable;[view addSubview:button];*y+=46;
}
- (void)finish:(NSView *)view y:(CGFloat)y { [view setFrameSize:NSMakeSize(view.frame.size.width,MAX(y,450))]; }
- (void)build {
  widgets=[[NSMutableDictionary alloc] init]; values=[[NSMutableDictionary alloc] init];
  panel=[[Dkc1PausePanel alloc] initWithContentRect:NSMakeRect(0,0,720,590)
      styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
  panel.title=@"DKC1Recomp — Paused"; panel.releasedWhenClosed=NO; panel.delegate=self;
  panel.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]; panel.minSize=NSMakeSize(400,380);
  panel.collectionBehavior=NSWindowCollectionBehaviorFullScreenAuxiliary;
  tabs=[[[NSTabView alloc] initWithFrame:NSMakeRect(12,62,696,516)] autorelease];
  tabs.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;[panel.contentView addSubview:tabs];
  status=[NSTextField labelWithString:@""];status.frame=NSMakeRect(16,10,680,36);status.maximumNumberOfLines=2;
  status.autoresizingMask=NSViewWidthSizable;status.font=[NSFont systemFontOfSize:11];[panel.contentView addSubview:status];
  NSView *view=[self page:@"Game"];CGFloat y=16;
  [self heading:@"Donkey Kong Country" view:view y:&y];
  [self text:@"Escape resumes the game. Graphics changes preview immediately behind this window." view:view y:&y height:44];
  [self button:@"Resume Game" tag:0 view:view y:&y];
  [self button:@"Quick Save" tag:kDkc1MacMenuQuickSave view:view y:&y];
  [self button:@"Quick Load" tag:kDkc1MacMenuQuickLoad view:view y:&y];
  [self button:@"Quit to Desktop" tag:kDkc1MacMenuQuit view:view y:&y];
  [self text:@"Keyboard: Escape opens this menu. Controller: Guide or Start + Back. D-pad moves focus; A selects; B resumes; shoulders switch tabs." view:view y:&y height:60];[self finish:view y:y];
  view=[self page:@"Graphics"];y=16;
  [self heading:@"Display" view:view y:&y];
  [self option:@"display" title:@"Display" labels:@[@"Flat panel",@"CRT television"] view:view y:&y max:0];
  [self option:@"upscaler" title:@"Upscaler" labels:@[@"Nearest (pixel exact)",@"Bilinear",@"Reconstruct",@"Sharp bilinear"] view:view y:&y max:0];
  [self option:@"screen" title:@"Phosphor colors" labels:@[@"Raw",@"CRT",@"Composite",@"Trinitron"] view:view y:&y max:0];
  [self heading:@"HD scenery" view:view y:&y];
  [self option:@"hd_polish" title:@"Edge cleanup" labels:nil view:view y:&y max:100];
  [self option:@"hd_finish" title:@"Grounded finish" labels:nil view:view y:&y max:100];
  [self text:@"Edge cleanup softens verified scenery silhouettes. Grounded finish adds a filmic curve and stable luminance grain; 33% is the original finish and 100% is 3× strength. Zero shows raw HD art. F10 compares HD with the original." view:view y:&y height:58];
  [self heading:@"Reconstruct" view:view y:&y];
  [self option:@"reconstruct_mode" title:@"Detail" labels:@[@"Sharp pixels only",@"+ Dither decoding",@"+ Diagonal edges",@"+ Level-2 slopes",@"+ Level-3 slopes"] view:view y:&y max:0];
  [self option:@"strength" title:@"Edge strength" labels:nil view:view y:&y max:100];
  [self option:@"softness" title:@"Softness" labels:nil view:view y:&y max:100];
  [self option:@"shading" title:@"Smooth shading" labels:nil view:view y:&y max:100];
  [self text:@"Reconstruct decodes dithers and softens shading while preserving outlines. CRT television uses its own beam simulation." view:view y:&y height:44];
  [self heading:@"CRT television" view:view y:&y];
  [self option:@"crt.preset" title:@"Tube preset" labels:@[@"Living room",@"Studio monitor",@"Soft",@"Custom"] view:view y:&y max:0];
  [self option:@"crt.scanlines" title:@"Scanlines" labels:nil view:view y:&y max:100];
  [self option:@"crt.sharpness" title:@"Sharpness" labels:nil view:view y:&y max:100];
  [self option:@"crt.mask" title:@"Phosphor mask" labels:@[@"None",@"Aperture grille, fine",@"Aperture grille, coarse",@"Slot mask"] view:view y:&y max:0];
  [self option:@"crt.mask_strength" title:@"Mask strength" labels:nil view:view y:&y max:100];
  [self option:@"crt.glow" title:@"Glow" labels:nil view:view y:&y max:100];
  [self option:@"crt.halation" title:@"Halation" labels:nil view:view y:&y max:100];
  [self option:@"crt.curvature" title:@"Curvature" labels:nil view:view y:&y max:100];
  [self text:@"The beam and phosphor mask fade out at small window sizes. Settings are remembered automatically." view:view y:&y height:44];[self finish:view y:y];
  view=[self page:@"Settings"];y=16;
  [self option:@"window_scale" title:@"Window scale" labels:nil view:view y:&y max:4];
  [self option:@"fullscreen" title:@"Window mode" labels:@[@"Windowed",@"Full screen"] view:view y:&y max:0];
  [self option:@"aspect" title:@"Aspect ratio" labels:@[@"4:3 (Native)",@"16:10",@"16:9"] view:view y:&y max:0];
  [self option:@"edge" title:@"Level edge" labels:@[@"Reflect terrain",@"Black margins",@"Shift inward",@"Glide inward"] view:view y:&y max:0];
  [self heading:@"Audio" view:view y:&y];
  [self option:@"audio_enabled" title:@"Audio" labels:@[@"Muted",@"Enabled"] view:view y:&y max:0];
  [self option:@"volume" title:@"Volume" labels:nil view:view y:&y max:100];
  [self button:@"Choose Replacement Music…" tag:kDkc1MacMenuChooseMusicPack view:view y:&y];
  [self button:@"Disable Replacement Music" tag:kDkc1MacMenuDisableMusicPack view:view y:&y];
  [self button:@"Restore Graphics and Audio Defaults" tag:1000 view:view y:&y];[self finish:view y:y];
  view=[self page:@"Controls"];y=16;
  [self heading:@"Players and Assist bindings" view:view y:&y];
  [self text:@"Configure both players’ keyboard and gamepad bindings, controller sources, and analog deadzones." view:view y:&y height:48];
  [self button:@"Configure Controls…" tag:kDkc1MacMenuControls view:view y:&y];
  [self text:@"F7 pauses; F8 steps a paused frame. F11 / Command-S saves; F12 / Command-L loads. Option-Return toggles full screen." view:view y:&y height:66];[self finish:view y:y];
  view=[self page:@"Assist"];y=16;
  NSButton *assist=[NSButton checkboxWithTitle:@"Enable Rewind and Fast-forward" target:self action:@selector(action:)];
  assistToggle=assist;
  assist.frame=NSMakeRect(12,y,400,26);assist.tag=1001;assist.state=controls->assist_enabled;[view addSubview:assist];y+=40;
  [self text:@"Default bindings: hold Backspace to rewind or Tab for 3× fast-forward. Gamepads use the left and right triggers. Sound is muted during either action." view:view y:&y height:66];
  [self option:@"state_slot" title:@"Save slot" labels:@[@"Slot 1 (Quick Save)",@"Slot 2",@"Slot 3",@"Slot 4",@"Slot 5"] view:view y:&y max:0];
  [self button:@"Save Selected Slot" tag:kDkc1MacMenuQuickSave view:view y:&y];
  [self button:@"Load Selected Slot" tag:kDkc1MacMenuQuickLoad view:view y:&y];
  [self text:@"Slot 1 uses your existing quicksave. Loading clears rewind history; save files remain separate from cartridge saves." view:view y:&y height:48];[self finish:view y:y];
  view=[self page:@"Mods"];y=16;
  [self heading:@"Upscaled HD Textures" view:view y:&y];
  [self button:@"Toggle HD Textures — Jungle Hijinxs only" tag:kDkc1MacMenuToggleHdTextures view:view y:&y];
  [self text:@"This preview currently covers the first level, Jungle Hijinxs, plus its connected bonus, hoard, and treehouse rooms. Unsupported scenes keep the original game textures." view:view y:&y height:68];
  [self heading:@"Dixie Kong Country" view:view y:&y];
  [self button:@"Switch Donkey / Dixie" tag:kDkc1MacMenuToggleDixie view:view y:&y];
  [self text:@"Dixie uses the verified built-in character mod and a separate save directory. Switching restarts into the selected runtime." view:view y:&y height:60];[self finish:view y:y];
  view=[self page:@"Credits"];y=16;
  [self heading:@"Donkey Kong Country — Native Recompilation" view:view y:&y];
  [self text:@"Original game by Rare and Nintendo. Native recompilation uses snesrecomp. Graphics and CRT models are adapted from DKC2Recomp; color profiles use the shared engine’s screen-color models.\n\nThis app uses your own game data. Third-party source notices are retained with the project." view:view y:&y height:150];[self finish:view y:y];
  [self refresh];
}
- (void)pollPad:(NSTimer *)unused {
  (void)unused; unsigned now=Dkc1MacPauseMenuController(),pressed=now&~lastPad; lastPad=now;
  if (!panel.isKeyWindow) return;
  if (pressed&kDkc1GamepadB || pressed&kDkc1GamepadGuide ||
      ((now&(kDkc1GamepadStart|kDkc1GamepadBack))==(kDkc1GamepadStart|kDkc1GamepadBack) && (pressed&(kDkc1GamepadStart|kDkc1GamepadBack)))) { [NSApp stopModal]; return; }
  if (pressed&(kDkc1GamepadLeftShoulder|kDkc1GamepadRightShoulder)) {
    NSInteger index=[tabs indexOfTabViewItem:tabs.selectedTabViewItem];
    index=(index+(pressed&kDkc1GamepadRightShoulder ? 1 : tabs.numberOfTabViewItems-1))%tabs.numberOfTabViewItems;
    [tabs selectTabViewItemAtIndex:index];
  }
  if (pressed&kDkc1GamepadDpadDown) [panel selectNextKeyView:nil];
  if (pressed&kDkc1GamepadDpadUp) [panel selectPreviousKeyView:nil];
  id focus=panel.firstResponder;
  if (pressed&kDkc1GamepadA && [focus respondsToSelector:@selector(performClick:)]) [focus performClick:nil];
  int delta=pressed&kDkc1GamepadDpadRight ? 1 : pressed&kDkc1GamepadDpadLeft ? -1 : 0;
  if (delta && [focus isKindOfClass:[NSSlider class]]) {
    [focus setIntValue:[focus intValue]+delta*5]; [self changed:focus];
  } else if (delta && [focus isKindOfClass:[NSPopUpButton class]]) {
    NSInteger index=MAX(0,MIN([focus numberOfItems]-1,[focus indexOfSelectedItem]+delta));
    [focus selectItemAtIndex:index]; [self changed:focus];
  }
}
- (void)dealloc { [widgets release];[values release];[panel release];[super dealloc]; }
@end
int Dkc1MacShowPauseMenu(void *window, Dkc1GraphicsSettings *settings,
                         Dkc1Controls *controls, int graphics_page) {
  if (s_open || !window) return 0;
  @autoreleasepool {
    s_open=YES; NSWindow *parent=window;
    Dkc1PauseMenu *menu=[[Dkc1PauseMenu alloc] init];menu->draft=*settings;menu->controls=controls;[menu build];
    NSRect visible=parent.screen.visibleFrame, frame=menu->panel.frame;
    frame.size.width=MIN(frame.size.width,visible.size.width-32); frame.size.height=MIN(frame.size.height,visible.size.height-32);
    frame.origin=NSMakePoint(NSMidX(parent.frame)-frame.size.width/2,NSMidY(parent.frame)-frame.size.height/2);
    frame.origin.x=MAX(NSMinX(visible),MIN(frame.origin.x,NSMaxX(visible)-frame.size.width));
    frame.origin.y=MAX(NSMinY(visible),MIN(frame.origin.y,NSMaxY(visible)-frame.size.height));
    [menu->panel setFrame:frame display:NO];
    NSView *dim=[[[NSView alloc] initWithFrame:parent.contentView.bounds] autorelease];
    dim.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;dim.wantsLayer=YES;
    dim.layer.backgroundColor=[[NSColor blackColor] colorWithAlphaComponent:0.30].CGColor;
    [parent.contentView addSubview:dim positioned:NSWindowAbove relativeTo:nil];
    [parent addChildWindow:menu->panel ordered:NSWindowAbove];
    if (graphics_page) [menu->tabs selectTabViewItemAtIndex:1];
    menu->lastPad=Dkc1MacPauseMenuController();
    menu->timer=[NSTimer timerWithTimeInterval:1.0/60.0 target:menu selector:@selector(pollPad:) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:menu->timer forMode:NSModalPanelRunLoopMode];
    [NSApp runModalForWindow:menu->panel];
    [menu->timer invalidate];[parent removeChildWindow:menu->panel];[menu->panel orderOut:nil];[dim removeFromSuperview];
    [parent makeKeyAndOrderFront:nil];int resume=menu->resumeGame;[menu release];s_open=NO;return resume;
  }
}
