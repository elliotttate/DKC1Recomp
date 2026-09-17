#import "macos_controls.h"
#import <AppKit/AppKit.h>
#include <SDL.h>
#include <string.h>

static NSString *const kControlsPreference = @"HostControlsV1";
static const char *kLogicalNames[12] = {
  "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "L", "R", "Start", "Select"
};
static void Defaults(Dkc1Controls *c) {
  memset(c, 0, sizeof *c);
  c->source[0] = kDkc1InputSourceBoth;
  const int keys[12] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_S, SDL_SCANCODE_Z,
    SDL_SCANCODE_A, SDL_SCANCODE_X, SDL_SCANCODE_Q, SDL_SCANCODE_W,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT};
  const int pads[12] = {12,13,14,15,2,1,4,3,10,11,7,5};
  memcpy(c->keys[0], keys, sizeof keys);
  for (int p = 0; p < 2; p++) {
    c->deadzone[p] = 25;
    memcpy(c->pads[p], pads, sizeof pads);
  }
  c->assist_keys[0] = SDL_SCANCODE_BACKSPACE;
  c->assist_keys[1] = SDL_SCANCODE_TAB;
  c->assist_pads[0] = DKC1_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
  c->assist_pads[1] = DKC1_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
}

static BOOL ValidKey(int key) {
  return key >= 0 && key < SDL_NUM_SCANCODES && key != SDL_SCANCODE_ESCAPE &&
      !(key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) &&
      key != SDL_SCANCODE_LGUI && key != SDL_SCANCODE_RGUI &&
      (!key || *SDL_GetScancodeName((SDL_Scancode)key));
}
static BOOL ValidPad(int pad) {
  return (pad >= 0 && pad <= 15) || (pad >= 100 && pad <= 111);
}
static void LoadInts(NSDictionary *d, NSString *name, int *values, int count,
                     int kind) {
  id a = d[name];
  if (![a isKindOfClass:[NSArray class]] || [a count] != count) return;
  for (int i = 0; i < count; i++) {
    if (![a[i] isKindOfClass:[NSNumber class]]) continue;
    int v = [a[i] intValue];
    BOOL valid = kind == 0 ? ValidKey(v) : kind == 1 ? ValidPad(v) :
                 kind == 2 ? v >= 0 && v <= 3 : v >= 0 && v <= 100;
    if (valid) values[i] = v;
  }
}
void Dkc1MacLoadControls(Dkc1Controls *c) {
  Defaults(c);
  NSDictionary *d = [[NSUserDefaults standardUserDefaults]
      dictionaryForKey:kControlsPreference];
  if (!d) return;
  LoadInts(d, @"sources", c->source, 2, 2);
  LoadInts(d, @"deadzones", c->deadzone, 2, 3);
  LoadInts(d, @"keys", &c->keys[0][0], 24, 0);
  LoadInts(d, @"pads", &c->pads[0][0], 24, 1);
  LoadInts(d, @"assistKeys", c->assist_keys, 4, 0);
  LoadInts(d, @"assistPads", c->assist_pads, 4, 1);
  c->assist_enabled = [d[@"assistEnabled"] boolValue];
}
static NSArray *Ints(const int *values, int count) {
  NSMutableArray *a = [NSMutableArray arrayWithCapacity:count];
  for (int i = 0; i < count; i++) [a addObject:@(values[i])];
  return a;
}
void Dkc1MacSaveControls(const Dkc1Controls *c) {
  [[NSUserDefaults standardUserDefaults] setObject:@{
    @"sources":Ints(c->source, 2), @"deadzones":Ints(c->deadzone, 2),
    @"keys":Ints(&c->keys[0][0], 24), @"pads":Ints(&c->pads[0][0], 24),
    @"assistKeys":Ints(c->assist_keys, 4), @"assistPads":Ints(c->assist_pads, 4),
    @"assistEnabled":@(c->assist_enabled != 0)
  } forKey:kControlsPreference];
}
static void Label(NSView *view, NSString *text, NSRect rect) {
  NSTextField *label = [NSTextField labelWithString:text];
  label.frame = rect;
  [view addSubview:label];
}
static NSPopUpButton *Binding(NSView *view, int value, BOOL pad, NSRect rect) {
  NSPopUpButton *menu = [[[NSPopUpButton alloc] initWithFrame:rect
                                               pullsDown:NO] autorelease];
  [menu addItemWithTitle:@"Unbound"];
  menu.lastItem.tag = 0;
  if (pad) {
    NSArray *names = @[@"A (bottom)", @"B (right)", @"X (left)", @"Y (top)",
      @"Back", @"Guide", @"Start", @"Left stick click", @"Right stick click",
      @"Left shoulder", @"Right shoulder", @"D-pad up / left stick up",
      @"D-pad down / left stick down", @"D-pad left / left stick left",
      @"D-pad right / left stick right"];
    for (int i = 0; i < 15; i++) {
      [menu addItemWithTitle:names[i]]; menu.lastItem.tag = i + 1;
    }
    NSArray *axes = @[@"Left stick X", @"Left stick Y", @"Right stick X",
                      @"Right stick Y", @"Left trigger", @"Right trigger"];
    for (int i = 0; i < 6; i++) for (int positive = 0; positive < 2; positive++) {
      [menu addItemWithTitle:[NSString stringWithFormat:@"%@ %@", axes[i],
                              positive ? @"+" : @"−"]];
      menu.lastItem.tag = DKC1_PAD_AXIS(i, positive);
    }
  } else {
    for (int i = 1; i < SDL_NUM_SCANCODES; i++) if (ValidKey(i)) {
      NSString *name = [NSString stringWithUTF8String:
                          SDL_GetScancodeName((SDL_Scancode)i)];
      /* SDL gives Return and Return2 the same name. NSPopUpButton replaces
       * duplicate titles, which otherwise silently erases the Return binding. */
      while ([menu itemWithTitle:name]) name = [name stringByAppendingString:@" (alternate)"];
      [menu addItemWithTitle:name];
      menu.lastItem.tag = i;
    }
  }
  [menu selectItemWithTag:value];
  [view addSubview:menu];
  return menu;
}

int Dkc1MacEditControls(Dkc1Controls *c) {
  @autoreleasepool {
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    alert.messageText = @"Controls and Assist";
    alert.informativeText = @"Pads follow connection order. The left stick also acts as the D-pad.\nFunction keys remain debugger shortcuts. Assist pauses sound during rewind or fast-forward.";
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"Restore Defaults"];
    NSView *view = [[[NSView alloc] initWithFrame:NSMakeRect(0,0,840,570)] autorelease];
    NSPopUpButton *sources[2], *keys[2][12], *pads[2][12];
    NSTextField *deadzones[2];
    for (int p = 0; p < 2; p++) {
      int x = p * 425;
      Label(view, [NSString stringWithFormat:@"Player %d", p + 1], NSMakeRect(x,542,100,22));
      sources[p] = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(x+90,538,210,28)
                                                        pullsDown:NO] autorelease];
      [sources[p] addItemsWithTitles:@[@"None", @"Keyboard", @"Gamepad", @"Keyboard + Gamepad"]];
      [sources[p] selectItemAtIndex:c->source[p]];
      [view addSubview:sources[p]];
      Label(view, @"Deadzone %", NSMakeRect(x,510,100,22));
      deadzones[p] = [[[NSTextField alloc] initWithFrame:NSMakeRect(x+100,508,55,24)] autorelease];
      deadzones[p].integerValue = c->deadzone[p];
      [view addSubview:deadzones[p]];
      Label(view, @"Keyboard", NSMakeRect(x+70,480,130,22));
      Label(view, @"Gamepad", NSMakeRect(x+208,480,170,22));
      for (int i = 0; i < 12; i++) {
        int y = 450 - 26*i;
        Label(view, [NSString stringWithUTF8String:kLogicalNames[i]], NSMakeRect(x,y,60,22));
        keys[p][i] = Binding(view,c->keys[p][i],NO,NSMakeRect(x+65,y-3,130,26));
        pads[p][i] = Binding(view,c->pads[p][i],YES,NSMakeRect(x+200,y-3,215,26));
      }
    }
    NSButton *assist = [NSButton checkboxWithTitle:@"Enable Assist — hold Rewind or 3× Fast-forward"
                                           target:nil action:nil];
    assist.frame = NSMakeRect(0,130,600,24);
    assist.state = c->assist_enabled ? NSControlStateValueOn : NSControlStateValueOff;
    [view addSubview:assist];
    NSPopUpButton *assistKeys[4], *assistPads[4];
    NSArray *actions = @[@"Rewind", @"Fast-forward", @"Quick save", @"Quick load"];
    for (int i = 0; i < 4; i++) {
      int x = (i / 2) * 425, y = 91 - (i % 2)*30;
      Label(view, actions[i], NSMakeRect(x,y,95,22));
      assistKeys[i] = Binding(view,c->assist_keys[i],NO,NSMakeRect(x+95,y-3,125,26));
      assistPads[i] = Binding(view,c->assist_pads[i],YES,NSMakeRect(x+220,y-3,195,26));
    }
    Label(view, @"Rewind uses a bounded memory history. Quick save/load also remain available in the Game menu.",
          NSMakeRect(0,12,830,28));
    alert.accessoryView = view;
    NSModalResponse response = [alert runModal];
    if (response == NSAlertSecondButtonReturn) return 0;
    if (response == NSAlertThirdButtonReturn) Defaults(c);
    else {
      for (int p = 0; p < 2; p++) {
        c->source[p] = (int)sources[p].indexOfSelectedItem;
        c->deadzone[p] = (int)MAX(0, MIN(100, deadzones[p].integerValue));
        for (int i = 0; i < 12; i++) {
          c->keys[p][i] = (int)keys[p][i].selectedItem.tag;
          c->pads[p][i] = (int)pads[p][i].selectedItem.tag;
        }
      }
      c->assist_enabled = assist.state == NSControlStateValueOn;
      for (int i = 0; i < 4; i++) {
        c->assist_keys[i] = (int)assistKeys[i].selectedItem.tag;
        c->assist_pads[i] = (int)assistPads[i].selectedItem.tag;
      }
    }
    Dkc1MacSaveControls(c);
    return 1;
  }
}
