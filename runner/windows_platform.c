/* Native Windows implementation of the SDL host's platform UI contract.
 * Mac-prefixed entry points retain ABI compatibility with the untouched Mac UI.
 * Settings live in the Windows user directory, never in a ROM or app bundle. */
#include "windows_platform.h"
#include "windows_music_pack.h"
#include "verified_rom.h"
#include "macos_file_picker.h"
#include "macos_controls.h"
#include "macos_pause_menu.h"
#include "macos_metal_presenter.h"
#include <SDL_syswm.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static HWND s_window,s_panel;
static SDL_Window *s_sdl;
static HMENU s_menu;
static HBRUSH s_dark;
static HFONT s_font;
static UINT s_dpi=96;
static int Px(int value){return MulDiv(value,(int)s_dpi,96);}
static int s_pending,s_page,s_resume,s_player;
static unsigned s_last_pad;
static Dkc1GraphicsSettings *s_graphics;
static Dkc1GraphicsSettings s_graphics_draft;
static Dkc1Controls *s_controls;
static wchar_t s_config[4096];
typedef struct Item { wchar_t text[160];int top; } Item;
static Item s_items[100];static int s_item_count;
typedef struct Field { const char *name,*label,*options;size_t offset;int low,high,page; } Field;
#define F(n,label,options,lo,hi,page) {#n,label,options,offsetof(Dkc1GraphicsSettings,n),lo,hi,page}
static const Field fields[]={
 F(display,"Display","Flat|CRT television",0,1,1),
 F(upscaler,"Upscaler","Nearest|Bilinear|Reconstruct|Sharp Bilinear",0,3,1),
 F(screen,"Phosphor colors","Raw|CRT|Composite|Trinitron",0,3,1),
 F(reconstruct_mode,"Reconstruction","Sharp pixels|Dither decoding|Diagonal edges|Level-2 slopes|Level-3 slopes",0,4,1),
 F(strength,"Edge strength",NULL,0,100,1),F(softness,"Softness",NULL,0,100,1),F(shading,"Smooth shading",NULL,0,100,1),
 F(crt.preset,"CRT preset","Living room|Studio monitor|Soft|Custom",0,3,2),
 F(crt.scanlines,"Scanlines",NULL,0,100,2),F(crt.sharpness,"Sharpness",NULL,0,100,2),
 F(crt.mask,"Phosphor mask","None|Fine grille|Coarse grille|Slot mask",0,3,2),
 F(crt.mask_strength,"Mask strength",NULL,0,100,2),F(crt.glow,"Glow",NULL,0,100,2),
 F(crt.halation,"Halation",NULL,0,100,2),F(crt.curvature,"Curvature",NULL,0,100,2),
 F(window_scale,"Window scale",NULL,1,4,3),F(fullscreen,"Fullscreen","Windowed|Fullscreen",0,1,3),
 F(aspect,"Aspect ratio","4:3|16:10|16:9",0,2,3),F(edge,"Level edge","Reflect|Black bars|Shift|Glide",0,3,3),
 F(audio_enabled,"Audio","Muted|Enabled",0,1,3),F(volume,"Volume",NULL,0,100,3),
 F(state_slot,"Save-state slot","Slot 1|Slot 2|Slot 3|Slot 4|Slot 5",0,4,3),F(aquatic_fixes,"Aquatic fixes (restart)","Off|Experimental opt-in",0,1,3)
};
#undef F
static void ConfigPath(void){
 if(s_config[0])return;
 const char *override=getenv("DKC1_USER_DIR");char *path=override?SDL_strdup(override):SDL_GetPrefPath("Flat2VR","DKC1Recomp");
 char file[4096];snprintf(file,sizeof file,"%s/windows.ini",path?path:".");SDL_free(path);
 MultiByteToWideChar(CP_UTF8,0,file,-1,s_config,4096);
}
static int ReadInt(const wchar_t *section,const char *name,int fallback){
 ConfigPath();wchar_t key[128];MultiByteToWideChar(CP_UTF8,0,name,-1,key,128);
 return (int)GetPrivateProfileIntW(section,key,fallback,s_config);
}
static void WriteInt(const wchar_t *section,const char *name,int value){
 ConfigPath();wchar_t key[128],text[32];MultiByteToWideChar(CP_UTF8,0,name,-1,key,128);
 swprintf_s(text,32,L"%d",value);WritePrivateProfileStringW(section,key,text,s_config);
}
int Dkc1WindowsSavedHaptics(void){return ReadInt(L"Host","Haptics",1)!=0;}
void Dkc1WindowsSetHaptics(int enabled){WriteInt(L"Host","Haptics",enabled!=0);}
int Dkc1WindowsSavedFrameGen(void){return ReadInt(L"Host","FrameGen",0)!=0;}
void Dkc1WindowsSetFrameGen(int enabled){WriteInt(L"Host","FrameGen",enabled!=0);}
int Dkc1WindowsSavedSquarePixels(void){return ReadInt(L"Host","SquarePixels",0)!=0;}
void Dkc1WindowsSetSquarePixels(int enabled){WriteInt(L"Host","SquarePixels",enabled!=0);}
static char *ReadPath(const wchar_t *key){
 ConfigPath();wchar_t path[4096];GetPrivateProfileStringW(L"Paths",key,L"",path,4096,s_config);
 if(!path[0])return NULL;char *out=malloc(16384);if(out)WideCharToMultiByte(CP_UTF8,0,path,-1,out,16384,NULL,NULL);return out;
}
static void WritePath(const wchar_t *key,const char *path){
 ConfigPath();wchar_t wide[4096];if(path)MultiByteToWideChar(CP_UTF8,0,path,-1,wide,4096);
 WritePrivateProfileStringW(L"Paths",key,path?wide:NULL,s_config);
}
void Dkc1MacSaveGraphics(const Dkc1GraphicsSettings *s){
 for(size_t i=0;i<sizeof fields/sizeof *fields;i++)WriteInt(L"GraphicsV1",fields[i].name,*(const int*)((const char*)s+fields[i].offset));
}
void Dkc1MacLoadGraphics(Dkc1GraphicsSettings *s){
 Dkc1GraphicsDefault(s);
 for(size_t i=0;i<sizeof fields/sizeof *fields;i++){int *v=(int*)((char*)s+fields[i].offset);*v=ReadInt(L"GraphicsV1",fields[i].name,*v);}
 const char *v=getenv("DKC1_DISPLAY");if(v)Dkc1CrtDisplayFromName(v,&s->display);
 v=getenv("DKC1_CRT_PRESET");int preset;if(v&&Dkc1CrtPresetFromName(v,&preset))Dkc1CrtSettingsApplyPreset(&s->crt,preset);
 const char *names[]={"nearest","bilinear","reconstruct","sharp-bilinear"};v=getenv("DKC1_UPSCALER");if(v)for(int i=0;i<4;i++)if(!strcmp(v,names[i]))s->upscaler=i;
 const char *colors[]={"raw","crt","composite","trinitron"};v=getenv("DKC1_SCREEN");if(v)for(int i=0;i<4;i++)if(!strcmp(v,colors[i]))s->screen=i;
 const char *keys[]={"DKC1_RECONSTRUCT_MODE","DKC1_RECONSTRUCT_STRENGTH","DKC1_RECONSTRUCT_SOFTNESS","DKC1_RECONSTRUCT_SHADING"};
 int *values[]={&s->reconstruct_mode,&s->strength,&s->softness,&s->shading};for(int i=0;i<4;i++){v=getenv(keys[i]);if(v)*values[i]=atoi(v);}
 Dkc1GraphicsClamp(s);
 const char *flags[]={"DKC1_WS_PIXEL_BOUNDARIES","DKC1_WS_LIVE_SCROLL","DKC1_WS_SCROLL_REBASE","DKC1_WS_WALL_ADJACENCY","DKC1_WS_WALL_SEAMS"};
 for(int i=0;i<5;i++)if(!getenv(flags[i]))_putenv_s(flags[i],s->aquatic_fixes?"1":"0");
}
static void Defaults(Dkc1Controls *c){
 memset(c,0,sizeof *c);c->source[0]=3;
 const int keys[]={82,81,80,79,22,29,4,27,20,26,40,229};
 const int pads[]={12,13,14,15,2,1,4,3,10,11,7,5};memcpy(c->keys[0],keys,sizeof keys);
 for(int p=0;p<2;p++){c->deadzone[p]=25;memcpy(c->pads[p],pads,sizeof pads);}
 c->assist_keys[0]=SDL_SCANCODE_BACKSPACE;c->assist_keys[1]=SDL_SCANCODE_TAB;c->assist_pads[0]=109;c->assist_pads[1]=111;
}
static void ControlsIo(Dkc1Controls *c,int save){
 /* Explicit integer keys, no serialized ABI-dependent structs. */
 const char *names[]={"source","deadzone","keys","pads","assist_keys","assist_pads","assist_enabled"};
 int *arrays[]={c->source,c->deadzone,&c->keys[0][0],&c->pads[0][0],c->assist_keys,c->assist_pads,&c->assist_enabled};
 const int counts[]={2,2,24,24,4,4,1};
 for(int group=0;group<7;group++)for(int i=0;i<counts[group];i++){
  char key[80];snprintf(key,sizeof key,"%s%d",names[group],i);int *value=&arrays[group][i];
  if(save)WriteInt(L"ControlsV1",key,*value);else{
   int v=ReadInt(L"ControlsV1",key,*value);bool valid=group==0?v>=0&&v<=3:group==1?v>=0&&v<=100:
    group==2||group==4?v>=0&&v<SDL_NUM_SCANCODES:group==6?v==0||v==1:(v>=0&&v<=15)||(v>=100&&v<=111);
   if(valid)*value=v;
  }
 }
}
void Dkc1MacSaveControls(const Dkc1Controls *c){Dkc1Controls copy=*c;ControlsIo(&copy,1);}
void Dkc1MacLoadControls(Dkc1Controls *c){Defaults(c);ControlsIo(c,0);}
static char *Pick(const wchar_t *title,const wchar_t *filter){
 wchar_t path[4096]={0};OPENFILENAMEW of={sizeof of};of.hwndOwner=s_panel?s_panel:s_window;of.lpstrTitle=title;
 of.lpstrFilter=filter;of.lpstrFile=path;of.nMaxFile=4096;of.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
 if(!GetOpenFileNameW(&of))return NULL;char *out=malloc(16384);if(out)WideCharToMultiByte(CP_UTF8,0,path,-1,out,16384,NULL,NULL);return out;
}
char *Dkc1WindowsPickRom(void){
 char error[192];size_t size;
 for(;;){char *path=Pick(L"Choose your DKC1 USA v1.0 ROM",L"SNES ROM\0*.sfc;*.smc\0All files\0*.*\0");
  if(!path)return NULL;uint8_t *rom=Dkc1ReadVerifiedRom(path,&size,error,sizeof error);
  if(rom){free(rom);WritePath(L"DKC1",path);return path;}
  free(path);SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Unsupported ROM",error,s_sdl);
 }
}
char *Dkc1MacChooseRom(void){
 char error[192];size_t size;char *saved=ReadPath(L"DKC1");
 if(saved){uint8_t *rom=Dkc1ReadVerifiedRom(saved,&size,error,sizeof error);
  if(rom){free(rom);return saved;}free(saved);WritePath(L"DKC1",NULL);}
 return Dkc1WindowsPickRom();
}


char *Dkc1MacSavedMsu1(void){return ReadPath(L"MSU1");}
void Dkc1MacClearMsu1(void){WritePath(L"MSU1",NULL);}
char *Dkc1MacChooseMsu1(void){
 int kind=MessageBoxW(s_panel?s_panel:s_window,L"Choose an extracted folder?\n\nYes: folder\nNo: .msu1 or .zip archive",L"Choose MSU-1 music pack",MB_YESNOCANCEL|MB_ICONQUESTION);
 if(kind==IDCANCEL)return NULL;
 if(kind==IDNO){char *archive=Pick(L"Choose MSU-1 archive",L"MSU-1 pack\0*.msu1;*.zip\0\0");if(!archive)return NULL;
  const char *override=getenv("DKC1_USER_DIR");char *root=override?SDL_strdup(override):SDL_GetPrefPath("Flat2VR","DKC1Recomp");char error[256];
  char *out=Dkc1WindowsExtractMusicPack(archive,root?root:".",error,sizeof error);free(archive);SDL_free(root);
  if(out)WritePath(L"MSU1",out);else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Music pack",error,s_sdl);return out;}
 BROWSEINFOW bi={0};bi.hwndOwner=s_panel?s_panel:s_window;bi.lpszTitle=L"Choose extracted MSU-1 folder containing track-N.pcm";
 bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&bi);if(!item)return NULL;
 wchar_t folder[MAX_PATH];BOOL ok=SHGetPathFromIDListW(item,folder);CoTaskMemFree(item);if(!ok)return NULL;
 char *out=malloc(4096);if(out){WideCharToMultiByte(CP_UTF8,0,folder,-1,out,4096,NULL,NULL);WritePath(L"MSU1",out);}return out;
}
Dkc1MacFullscreenScaling Dkc1MacSavedFullscreenScaling(void){return (Dkc1MacFullscreenScaling)ReadInt(L"Host","Scaling",1);}
void Dkc1MacSetFullscreenScaling(Dkc1MacFullscreenScaling v){WriteInt(L"Host","Scaling",v);}
Dkc1EdgePolicy Dkc1MacSavedWidescreenEdge(void){return (Dkc1EdgePolicy)ReadInt(L"GraphicsV1","edge",kDkc1EdgeGlide);}
void Dkc1MacSetWidescreenEdge(Dkc1EdgePolicy v){WriteInt(L"GraphicsV1","edge",v);}

static void Dark(HWND hwnd){BOOL yes=TRUE;COLORREF color=RGB(28,29,33);DwmSetWindowAttribute(hwnd,20,&yes,sizeof yes);DwmSetWindowAttribute(hwnd,35,&color,sizeof color);}
static void ThemeMenu(HMENU menu,int top){
 MENUINFO mi={sizeof mi};mi.fMask=MIM_BACKGROUND;mi.hbrBack=s_dark;SetMenuInfo(menu,&mi);
 for(int i=0;i<GetMenuItemCount(menu)&&s_item_count<100;i++){
  Item *item=&s_items[s_item_count++];item->top=top;GetMenuStringW(menu,i,item->text,160,MF_BYPOSITION);
  MENUITEMINFOW info={sizeof info};info.fMask=MIIM_DATA|MIIM_FTYPE;info.dwItemData=(ULONG_PTR)item;info.fType=MFT_OWNERDRAW;
  SetMenuItemInfoW(menu,i,TRUE,&info);HMENU child=GetSubMenu(menu,i);if(child)ThemeMenu(child,0);
 }
}
static LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
 (void)id;(void)data;
 if(msg==WM_COMMAND&&HIWORD(wp)==0&&!lp){s_pending=LOWORD(wp);return 0;}
 if(msg==WM_MEASUREITEM&&!wp){MEASUREITEMSTRUCT *m=(void*)lp;if(m->CtlType==ODT_MENU&&m->itemData){
  Item *item=(void*)m->itemData;HDC dc=GetDC(hwnd);HGDIOBJ old=SelectObject(dc,s_font);SIZE size;
  GetTextExtentPoint32W(dc,item->text,(int)wcslen(item->text),&size);m->itemWidth=size.cx+(item->top?16:58);m->itemHeight=size.cy+12;
  SelectObject(dc,old);ReleaseDC(hwnd,dc);return TRUE;}}
 if(msg==WM_DRAWITEM&&!wp){DRAWITEMSTRUCT *d=(void*)lp;if(d->CtlType==ODT_MENU&&d->itemData){
  Item *item=(void*)d->itemData;int saved=SaveDC(d->hDC);SetDCBrushColor(d->hDC,d->itemState&ODS_SELECTED?RGB(65,65,80):RGB(28,29,33));
  FillRect(d->hDC,&d->rcItem,(HBRUSH)GetStockObject(DC_BRUSH));SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,RGB(235,235,240));SelectObject(d->hDC,s_font);
  RECT r=d->rcItem;r.left+=item->top?8:28;r.right-=20;wchar_t label[160];wcscpy_s(label,160,item->text);wchar_t *tab=wcschr(label,L'\t');if(tab)*tab++=0;
  DrawTextW(d->hDC,label,-1,&r,DT_VCENTER|DT_SINGLELINE);if(tab)DrawTextW(d->hDC,tab,-1,&r,DT_VCENTER|DT_SINGLELINE|DT_RIGHT);
  if(d->itemState&ODS_CHECKED){r=d->rcItem;r.right=r.left+24;DrawTextW(d->hDC,L"\x2713",1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
  RestoreDC(d->hDC,saved);return TRUE;}}
 if(msg==WM_MENUCHAR){HMENU menu=(HMENU)lp;for(int i=0;i<GetMenuItemCount(menu);i++){wchar_t text[160];GetMenuStringW(menu,i,text,160,MF_BYPOSITION);wchar_t *a=wcschr(text,L'&');if(a&&towupper(a[1])==towupper((wchar_t)LOWORD(wp)))return MAKELRESULT(i,MNC_EXECUTE);}}
 return DefSubclassProc(hwnd,msg,wp,lp);
}
void Dkc1WindowsAttach(SDL_Window *window){
 s_sdl=window;SDL_SysWMinfo info;SDL_VERSION(&info.version);SDL_GetWindowWMInfo(window,&info);s_window=info.info.win.window;
 typedef UINT (WINAPI *GetDpiFn)(HWND);
 GetDpiFn getDpi=(GetDpiFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
 if(getDpi)s_dpi=getDpi(s_window);
 s_dark=CreateSolidBrush(RGB(28,29,33));NONCLIENTMETRICSW metrics={sizeof metrics};SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof metrics,&metrics,0);s_font=CreateFontIndirectW(&metrics.lfMenuFont);
 Dark(s_window);SetWindowSubclass(s_window,WindowProc,1,0);CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
}
static void Add(HMENU menu,int command,const wchar_t *text){AppendMenuW(menu,MF_STRING,command,text);}
static HMENU Sub(HMENU parent,const wchar_t *text){HMENU menu=CreatePopupMenu();AppendMenuW(parent,MF_POPUP,(UINT_PTR)menu,text);return menu;}
void Dkc1MacInstallMenu(void){
 s_menu=CreateMenu();HMENU game=Sub(s_menu,L"&Game"),view=Sub(s_menu,L"&View"),mods=Sub(s_menu,L"&Mods"),music=Sub(s_menu,L"&Music");
 Add(game,kDkc1MacMenuPauseMenu,L"&Pause / Settings\tEsc");Add(game,kDkc1MacMenuControls,L"&Controls and Assist...");
 Add(game,kDkc1MacMenuChangeRom,L"Change &ROM...");
 Add(game,kDkc1MacMenuToggleHaptics,L"Controller &rumble (enemy stomps)");
 Add(game,kDkc1MacMenuTestHaptics,L"&Test controller rumble");
 Add(game,kDkc1MacMenuPause,L"Pause / Resume\tF7");Add(game,kDkc1MacMenuStep,L"Step one frame\tF8");
 Add(game,kDkc1MacMenuQuickSave,L"Quick &Save\tF11");Add(game,kDkc1MacMenuQuickLoad,L"Quick &Load\tF12");Add(game,kDkc1MacMenuExportRepro,L"Export repro bundle\tF9");Add(game,kDkc1MacMenuQuit,L"&Quit\tAlt+F4");
 Add(view,kDkc1MacMenuGraphics,L"&Graphics Settings...");Add(view,kDkc1MacMenuFullscreen,L"&Fullscreen\tAlt+Enter");
 Add(view,kDkc1MacMenuFrameGen,L"Smooth &animation / frame generation\tF10");
 HMENU pixels=Sub(view,L"Pixel as&pect");Add(pixels,kDkc1MacMenuPixelAspectSnes,L"SNES 7:6 (authentic CRT)");Add(pixels,kDkc1MacMenuPixelAspectSquare,L"Square pixels");
 HMENU aspect=Sub(view,L"&Aspect ratio");Add(aspect,kDkc1MacMenuAspectNative,L"4:3 (Native)");Add(aspect,kDkc1MacMenuAspect16x10,L"16:10 (308x224)");Add(aspect,kDkc1MacMenuAspect16x9,L"16:9 (342x224)");
 HMENU scaler=Sub(view,L"&Upscaler");Add(scaler,kDkc1MacMenuFullscreenPixelSharp,L"Nearest");Add(scaler,kDkc1MacMenuFullscreenSmooth,L"Bilinear");Add(scaler,kDkc1MacMenuUpscalerReconstruct,L"Reconstruct");Add(scaler,kDkc1MacMenuFullscreenSharpBilinear,L"Sharp Bilinear");
 HMENU display=Sub(view,L"&Display");Add(display,kDkc1MacMenuDisplayFlat,L"Flat");Add(display,kDkc1MacMenuDisplayCrt,L"CRT television");
 HMENU colors=Sub(view,L"Phosphor &colors");const wchar_t *colorNames[]={L"Raw",L"CRT",L"Composite",L"Trinitron"};for(int i=0;i<4;i++)Add(colors,kDkc1MacMenuScreenRaw+i,colorNames[i]);
 HMENU edges=Sub(view,L"Level &edge");const wchar_t *edgeNames[]={L"Reflect",L"Black bars",L"Shift",L"Glide"};for(int i=0;i<4;i++)Add(edges,kDkc1MacMenuEdgeReflect+i,edgeNames[i]);
 HMENU layers=Sub(view,L"&Layers");const wchar_t *layerNames[]={L"Composite",L"BG1",L"BG2",L"BG3",L"Sprites"};for(int i=0;i<5;i++)Add(layers,kDkc1MacMenuLayerComposite+i,layerNames[i]);Add(view,kDkc1MacMenuProvenance,L"Provenance\tF1");
 Add(mods,kDkc1MacMenuToggleDixie,L"&Dixie Kong Country");
 Add(music,kDkc1MacMenuChooseMusicPack,L"Choose &MSU-1 music pack...");Add(music,kDkc1MacMenuDisableMusicPack,L"&Disable replacement music");
 ThemeMenu(s_menu,1);SetMenu(s_window,s_menu);DrawMenuBar(s_window);
}
static void Check(int id,int checked){CheckMenuItem(s_menu,id,MF_BYCOMMAND|(checked?MF_CHECKED:MF_UNCHECKED));}
void Dkc1WindowsUpdateHapticsMenu(int enabled){
 if(!s_menu)return;
 Check(kDkc1MacMenuToggleHaptics,enabled);
 EnableMenuItem(s_menu,kDkc1MacMenuTestHaptics,MF_BYCOMMAND|(enabled?MF_ENABLED:MF_GRAYED));
}
void Dkc1WindowsUpdateHostMenu(int framegen_enabled,int square_pixels){
 if(!s_menu)return;
 Check(kDkc1MacMenuFrameGen,framegen_enabled);
 Check(kDkc1MacMenuPixelAspectSnes,!square_pixels);Check(kDkc1MacMenuPixelAspectSquare,square_pixels);
}
void Dkc1MacUpdateGraphicsMenuState(int display,int upscaler,int screen){
 Check(kDkc1MacMenuDisplayFlat,!display);Check(kDkc1MacMenuDisplayCrt,display);
 int scalers[]={kDkc1MacMenuFullscreenPixelSharp,kDkc1MacMenuFullscreenSmooth,kDkc1MacMenuUpscalerReconstruct,kDkc1MacMenuFullscreenSharpBilinear};
 for(int i=0;i<4;i++){Check(scalers[i],i==upscaler);Check(kDkc1MacMenuScreenRaw+i,i==screen);}
}
void Dkc1MacUpdateMenuState(int paused,int fullscreen,Dkc1MacFullscreenScaling scaling,Dkc1VideoAspect aspect,Dkc1EdgePolicy edge,unsigned char layers,int provenance,int music,int dixie,int hd_textures){
 (void)scaling;(void)hd_textures;Check(kDkc1MacMenuPause,paused);Check(kDkc1MacMenuFullscreen,fullscreen);Check(kDkc1MacMenuChooseMusicPack,music);Check(kDkc1MacMenuProvenance,provenance);Check(kDkc1MacMenuToggleDixie,dixie);
 for(int i=0;i<3;i++)Check(kDkc1MacMenuAspectNative+i,aspect==i);for(int i=0;i<4;i++)Check(kDkc1MacMenuEdgeReflect+i,edge==i);
 int masks[]={255,1,2,4,16};for(int i=0;i<5;i++)Check(kDkc1MacMenuLayerComposite+i,layers==masks[i]);
}
/* A Win32 menu bar is non-client area and stays drawn on the borderless
 * fullscreen popup SDL creates, so it is detached for fullscreen and restored
 * afterward. The HMENU and its checkmarks survive detachment. */
void Dkc1WindowsShowMenuBar(int visible){
 if(!s_window||!s_menu)return;
 if(!!visible==(GetMenu(s_window)!=NULL))return;
 SetMenu(s_window,visible?s_menu:NULL);DrawMenuBar(s_window);
}
int Dkc1WindowsMenuBarVisible(void){return s_window&&GetMenu(s_window)!=NULL;}
void Dkc1WindowsEvent(const SDL_Event *event){(void)event;if(s_pending){int command=s_pending;s_pending=0;Dkc1MacMenuCommand(command);}}
void Dkc1WindowsDetach(void){if(s_window){RemoveWindowSubclass(s_window,WindowProc,1);SetMenu(s_window,NULL);}if(s_menu)DestroyMenu(s_menu);if(s_font)DeleteObject(s_font);if(s_dark)DeleteObject(s_dark);s_window=NULL;s_menu=NULL;s_item_count=0;CoUninitialize();}

static HWND Child(const char *type,const char *text,int id,int x,int y,int w,int h,DWORD style){
 if(!strcmp(type,"BUTTON"))style=(style&~BS_TYPEMASK)|BS_OWNERDRAW;
 HWND child=CreateWindowExA(0,type,text,WS_CHILD|WS_VISIBLE|style,Px(x),Px(y),Px(w),Px(h),s_panel,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);SendMessageW(child,WM_SETFONT,(WPARAM)s_font,TRUE);return child;
}
static void ChoiceItem(HWND choice,const char *label,int value,int selected){int index=(int)SendMessageA(choice,CB_ADDSTRING,0,(LPARAM)label);SendMessageW(choice,CB_SETITEMDATA,index,value);if(value==selected)SendMessageW(choice,CB_SETCURSEL,index,0);}
static HWND Choice(int id,int x,int y,int w,const char *options,int low,int high,int selected){
 HWND c=Child("COMBOBOX","",id,x,y,w,240,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
 if(options){char copy[1024];strcpy_s(copy,sizeof copy,options);char *context=NULL,*part=strtok_s(copy,"|",&context);int value=low;while(part){ChoiceItem(c,part,value++,selected);part=strtok_s(NULL,"|",&context);}}
 else for(int v=low;v<=high;v++){char label[32];snprintf(label,sizeof label,"%d",v);ChoiceItem(c,label,v,selected);}return c;
}
static HWND Binding(int id,int x,int y,int w,int pad,int selected){
 HWND c=Child("COMBOBOX","",id,x,y,w,320,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);ChoiceItem(c,"Unbound",0,selected);
 if(pad){for(int i=0;i<15;i++){const char *name=SDL_GameControllerGetStringForButton(i);ChoiceItem(c,name?name:"Button",i+1,selected);}for(int i=0;i<6;i++)for(int sign=0;sign<2;sign++){char label[80];snprintf(label,sizeof label,"%s %s",SDL_GameControllerGetStringForAxis(i),sign?"+":"-");ChoiceItem(c,label,100+i*2+sign,selected);}}
 else for(int i=1;i<SDL_NUM_SCANCODES;i++){const char *name=SDL_GetScancodeName(i);if(name&&*name&&i!=SDL_SCANCODE_ESCAPE&&!(i>=SDL_SCANCODE_F1&&i<=SDL_SCANCODE_F12)&&i!=SDL_SCANCODE_LGUI&&i!=SDL_SCANCODE_RGUI)ChoiceItem(c,name,i,selected);}return c;
}
static void FillPanel(void){
 HWND child=GetWindow(s_panel,GW_CHILD);while(child){HWND next=GetWindow(child,GW_HWNDNEXT);DestroyWindow(child);child=next;}
 const char *pages[]={"Game","Graphics","CRT","Settings","Controls","Assist","Mods / Music","Credits"};for(int i=0;i<8;i++)Child("BUTTON",pages[i],1000+i,12+i*102,12,100,32,BS_PUSHBUTTON|WS_TABSTOP);
 Child("BUTTON","Resume Game",1100,12,656,180,34,BS_PUSHBUTTON|WS_TABSTOP);Child("BUTTON","Close panel",1101,206,656,150,34,BS_PUSHBUTTON|WS_TABSTOP);
 if(s_page>=1&&s_page<=3){int row=0;for(size_t i=0;i<sizeof fields/sizeof *fields;i++)if(fields[i].page==s_page){const Field *f=&fields[i];int y=65+row++*48;Child("STATIC",f->label,0,22,y+5,255,24,0);Choice(2000+(int)i,280,y,400,f->options,f->low,f->high,*(int*)((char*)s_graphics+f->offset));}
  if(s_page==3)Child("STATIC","Aquatic fixes are experimental and apply after restart. Defaults remain off.",0,22,480,790,40,0);
 }else if(s_page==0){
  Child("STATIC",Dkc1MacHostStatus(),0,22,75,780,60,0);Child("BUTTON","Quick Save (selected slot)",1200,22,160,290,38,BS_PUSHBUTTON);Child("BUTTON","Quick Load (selected slot)",1201,330,160,290,38,BS_PUSHBUTTON);Child("BUTTON","Quit to desktop",1202,22,220,290,38,BS_PUSHBUTTON);
 }else if(s_page==4){
  Choice(3000,22,60,150,"Player 1|Player 2",0,1,s_player);Choice(3001,190,60,260,"None|Keyboard|Gamepad|Keyboard + Gamepad",0,3,s_controls->source[s_player]);Child("STATIC","Deadzone (%)",0,480,65,140,24,0);Choice(3002,635,60,130,NULL,0,100,s_controls->deadzone[s_player]);
  const char *names[]={"Up","Down","Left","Right","A","B","X","Y","L","R","Start","Select"};
  for(int i=0;i<12;i++){int y=110+i*42;Child("STATIC",names[i],0,22,y+5,120,25,0);Binding(3100+i,150,y,275,0,s_controls->keys[s_player][i]);Binding(3200+i,440,y,360,1,s_controls->pads[s_player][i]);}
 }else if(s_page==5){
  Child("STATIC","Assist Tools",0,22,70,240,24,0);Choice(3300,280,65,300,"Disabled|Enabled",0,1,s_controls->assist_enabled);
  const char *names[]={"Rewind","Fast-forward (3x)","Quick Save","Quick Load"};for(int i=0;i<4;i++){int y=125+i*55;Child("STATIC",names[i],0,22,y+5,190,25,0);Binding(3310+i,215,y,275,0,s_controls->assist_keys[i]);Binding(3320+i,505,y,290,1,s_controls->assist_pads[i]);}
  Child("STATIC","Rewind uses at most 128 MiB. Save and load use the slot selected in Settings.",0,22,370,780,40,0);
 }else if(s_page==6){
  Child("BUTTON","Choose MSU-1 music folder or archive",3402,22,200,510,38,BS_PUSHBUTTON);Child("BUTTON","Disable replacement music",3403,22,250,510,38,BS_PUSHBUTTON);
  Child("STATIC","Music selection applies after restart. Dixie Kong Country lives in the Mods menu and needs no extra ROM.",0,22,320,780,60,0);
 }else Child("STATIC","DKC1Recomp\nOriginal game: Rare / Nintendo\nNative host and tools: project contributors\nSNESrecomp and SDL contributors\n\nNo ROM or game assets are included.",0,22,80,770,280,0);
 InvalidateRect(s_panel,NULL,TRUE);
}
static LRESULT CALLBACK PanelProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 if(msg==WM_DRAWITEM){
  DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;if(d->CtlType==ODT_BUTTON){
   HBRUSH brush=CreateSolidBrush((d->itemState&ODS_SELECTED)?RGB(58,78,108):RGB(43,45,52));FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);
   SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,RGB(235,235,240));wchar_t text[128];GetWindowTextW(d->hwndItem,text,128);
   DrawTextW(d->hDC,text,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
   if(d->itemState&ODS_FOCUS){RECT focus=d->rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(d->hDC,&focus);}return TRUE;
  }
 }
 if(msg==WM_TIMER){
  unsigned now=Dkc1MacPauseMenuController(),pressed=now&~s_last_pad;s_last_pad=now;
  if(GetForegroundWindow()!=hwnd)return 0;
  if((pressed&(kDkc1GamepadB|kDkc1GamepadGuide))||
     ((now&(kDkc1GamepadStart|kDkc1GamepadBack))==(kDkc1GamepadStart|kDkc1GamepadBack)&&
      (pressed&(kDkc1GamepadStart|kDkc1GamepadBack)))){s_resume=1;DestroyWindow(hwnd);s_panel=NULL;return 0;}
  if(pressed&(kDkc1GamepadLeftShoulder|kDkc1GamepadRightShoulder)){
   s_page=(s_page+((pressed&kDkc1GamepadRightShoulder)?1:7))%8;FillPanel();SetFocus(GetNextDlgTabItem(hwnd,NULL,FALSE));
  }
  if(pressed&(kDkc1GamepadDpadDown|kDkc1GamepadDpadUp))SendMessageW(hwnd,WM_NEXTDLGCTL,!!(pressed&kDkc1GamepadDpadUp),FALSE);
  HWND focus=GetFocus();wchar_t type[32]={0};if(focus)GetClassNameW(focus,type,32);
  if(IsChild(hwnd,focus)&&!wcscmp(type,L"ComboBox")){
   int index=(int)SendMessageW(focus,CB_GETCURSEL,0,0),count=(int)SendMessageW(focus,CB_GETCOUNT,0,0);
   int next=index+((pressed&kDkc1GamepadDpadRight)?1:(pressed&kDkc1GamepadDpadLeft)?-1:0);
   if(next>=0&&next<count&&next!=index){SendMessageW(focus,CB_SETCURSEL,next,0);SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(focus),CBN_SELCHANGE),(LPARAM)focus);}
  }else if(IsChild(hwnd,focus)&&!wcscmp(type,L"Button")&&(pressed&kDkc1GamepadA))SendMessageW(focus,BM_CLICK,0,0);
  return 0;
 }
 if(msg==WM_CLOSE){DestroyWindow(hwnd);s_panel=NULL;return 0;}
 if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX){SetTextColor((HDC)wp,RGB(235,235,240));SetBkColor((HDC)wp,RGB(28,29,33));return (LRESULT)s_dark;}
 if(msg==WM_COMMAND){int id=LOWORD(wp);
  if(id>=1000&&id<1008){s_page=id-1000;FillPanel();return 0;}
  if(id==1100||id==1101){s_resume=id==1100;DestroyWindow(hwnd);s_panel=NULL;return 0;}
  if(id==1202){Dkc1MacMenuCommand(kDkc1MacMenuQuit);DestroyWindow(hwnd);s_panel=NULL;return 0;}
  if(id==1200||id==1201){Dkc1MacMenuCommand(id==1200?kDkc1MacMenuQuickSave:kDkc1MacMenuQuickLoad);FillPanel();return 0;}
  if(id>=3402&&id<=3403){int commands[]={kDkc1MacMenuChooseMusicPack,kDkc1MacMenuDisableMusicPack};Dkc1MacMenuCommand(commands[id-3402]);return 0;}
  if(HIWORD(wp)==CBN_SELCHANGE){int index=(int)SendMessageW((HWND)lp,CB_GETCURSEL,0,0);int v=(int)SendMessageW((HWND)lp,CB_GETITEMDATA,index,0);
   if(id>=2000&&id<2000+(int)(sizeof fields/sizeof *fields)){
    const Field *f=&fields[id-2000];*(int*)((char*)s_graphics+f->offset)=v;
    if(f->offset==offsetof(Dkc1GraphicsSettings,crt.preset))Dkc1CrtSettingsApplyPreset(&s_graphics->crt,v);
    else if(f->page==2)s_graphics->crt.preset=kDkc1CrtPresetCustom;
    Dkc1MacApplyGraphics(s_graphics);if(f->offset==offsetof(Dkc1GraphicsSettings,crt.preset))FillPanel();
   }else{
    if(id==3000){s_player=v;FillPanel();return 0;}if(id==3001)s_controls->source[s_player]=v;if(id==3002)s_controls->deadzone[s_player]=v;
    if(id>=3100&&id<3112)s_controls->keys[s_player][id-3100]=v;if(id>=3200&&id<3212)s_controls->pads[s_player][id-3200]=v;
    if(id==3300)Dkc1MacAssistEnabled(v);if(id>=3310&&id<3314)s_controls->assist_keys[id-3310]=v;if(id>=3320&&id<3324)s_controls->assist_pads[id-3320]=v;
    Dkc1MacSaveControls(s_controls);
   }
  }return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}
int Dkc1MacPauseMenuIsOpen(void){return s_panel!=NULL;}
int Dkc1MacShowPauseMenu(void *window,Dkc1GraphicsSettings *graphics,Dkc1Controls *controls,int page){
 if(s_panel)return 0;
 /* Apply compares against the live host settings; edit a separate draft. */
 s_graphics_draft=*graphics;s_graphics=&s_graphics_draft;
 s_controls=controls;s_page=page;s_resume=0;
 WNDCLASSW wc={0};wc.lpfnWndProc=PanelProc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"DKC1Settings";wc.hbrBackground=s_dark;wc.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassW(&wc);
 RECT r;GetWindowRect(window,&r);int x=r.left+Px(30),y=r.top+Px(65);
 MONITORINFO monitor={sizeof monitor};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
 int width=Px(860),height=Px(740);
 if(x+width>monitor.rcWork.right)x=monitor.rcWork.right-width;
 if(y+height>monitor.rcWork.bottom)y=monitor.rcWork.bottom-height;
 if(x<monitor.rcWork.left)x=monitor.rcWork.left;if(y<monitor.rcWork.top)y=monitor.rcWork.top;
 /* A separate top-level dialog remains reachable by task switching and
  * accessibility tools. Gameplay is still disabled for its modal lifetime. */
 s_panel=CreateWindowExW(WS_EX_DLGMODALFRAME,L"DKC1Settings",L"DKC1Recomp - Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,x,y,width,height,NULL,NULL,wc.hInstance,NULL);
 if(!s_panel)return 0;Dark(s_panel);FillPanel();ShowWindow(s_panel,SW_SHOW);EnableWindow(window,FALSE);
 s_last_pad=Dkc1MacPauseMenuController();SetTimer(s_panel,1,16,NULL);SetFocus(GetNextDlgTabItem(s_panel,NULL,FALSE));
 MSG msg;while(s_panel&&GetMessageW(&msg,NULL,0,0)>0){if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){DestroyWindow(s_panel);s_panel=NULL;break;}if(!IsDialogMessageW(s_panel,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 EnableWindow(window,TRUE);SetForegroundWindow(window);return s_resume;
}
int Dkc1MacEditControls(Dkc1Controls *controls){Dkc1GraphicsSettings graphics;Dkc1MacLoadGraphics(&graphics);return Dkc1MacShowPauseMenu(s_window,&graphics,controls,4);}

int Dkc1WindowsPlatformTest(const char *directory){
 /* Isolated synthetic preference and menu contract test; no ROM required. */
 GUID id;wchar_t suffix[40],root[4096];
 if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(&id,suffix,40))return 1;
 MultiByteToWideChar(CP_UTF8,0,directory,-1,root,4096);
 if(swprintf_s(s_config,4096,L"%s/settings-%s.ini",root,suffix)<0)return 2;
 for(int sample=0;sample<3;sample++){
  Dkc1GraphicsSettings expected,actual;Dkc1GraphicsDefault(&expected);
  for(size_t i=0;i<sizeof fields/sizeof *fields;i++){
   const Field *f=&fields[i];*(int*)((char*)&expected+f->offset)=sample==0?f->low:sample==1?(f->low+f->high)/2:f->high;
  }
  Dkc1MacSaveGraphics(&expected);Dkc1MacLoadGraphics(&actual);
  if(memcmp(&expected,&actual,sizeof expected))return 3;
 }
 Dkc1Controls controls,loaded;Defaults(&controls);
 controls.source[1]=2;controls.deadzone[1]=73;controls.keys[1][11]=SDL_SCANCODE_SPACE;
 controls.pads[1][0]=101;controls.assist_enabled=1;controls.assist_keys[2]=SDL_SCANCODE_K;
 Dkc1MacSaveControls(&controls);Dkc1MacLoadControls(&loaded);
 if(memcmp(&controls,&loaded,sizeof controls))return 4;
 SDL_Window *window=SDL_CreateWindow("Synthetic menu test",0,0,400,300,SDL_WINDOW_HIDDEN);
 if(!window)return 5;Dkc1WindowsAttach(window);Dkc1MacInstallMenu();
 if(GetMenuItemCount(s_menu)!=4)return 6;
 if(!Dkc1WindowsSavedHaptics())return 15;
 for(int enabled=0;enabled<2;enabled++){
  Dkc1WindowsSetHaptics(enabled);if(Dkc1WindowsSavedHaptics()!=enabled)return 16;
  Dkc1WindowsUpdateHapticsMenu(enabled);
  if(!!(GetMenuState(s_menu,kDkc1MacMenuToggleHaptics,MF_BYCOMMAND)&MF_CHECKED)!=enabled)return 17;
  if(!!(GetMenuState(s_menu,kDkc1MacMenuTestHaptics,MF_BYCOMMAND)&MF_GRAYED)==enabled)return 18;
 }
 const int scalers[]={kDkc1MacMenuFullscreenPixelSharp,kDkc1MacMenuFullscreenSmooth,kDkc1MacMenuUpscalerReconstruct,kDkc1MacMenuFullscreenSharpBilinear};
 for(int selected=0;selected<4;selected++){
  Dkc1MacUpdateGraphicsMenuState(selected&1,selected,selected);
  for(int i=0;i<4;i++)if(!!(GetMenuState(s_menu,scalers[i],MF_BYCOMMAND)&MF_CHECKED)!=(i==selected))return 7;
  if(!(GetMenuState(s_menu,kDkc1MacMenuScreenRaw+selected,MF_BYCOMMAND)&MF_CHECKED))return 8;
 }
 for(int selected=0;selected<3;selected++){
  Dkc1MacUpdateMenuState(0,0,1,selected,3,255,0,0,selected&1,0);
  for(int i=0;i<3;i++)if(!!(GetMenuState(s_menu,kDkc1MacMenuAspectNative+i,MF_BYCOMMAND)&MF_CHECKED)!=(i==selected))return 9;
  if(!!(GetMenuState(s_menu,kDkc1MacMenuToggleDixie,MF_BYCOMMAND)&MF_CHECKED)!=(selected&1))return 14;
 }
 for(int framegen=0;framegen<2;framegen++)for(int square=0;square<2;square++){
  Dkc1WindowsUpdateHostMenu(framegen,square);
  if(!!(GetMenuState(s_menu,kDkc1MacMenuFrameGen,MF_BYCOMMAND)&MF_CHECKED)!=framegen)return 19;
  if(!!(GetMenuState(s_menu,kDkc1MacMenuPixelAspectSquare,MF_BYCOMMAND)&MF_CHECKED)!=square)return 20;
  if(!!(GetMenuState(s_menu,kDkc1MacMenuPixelAspectSnes,MF_BYCOMMAND)&MF_CHECKED)==square)return 21;
 }
 for(int enabled=0;enabled<2;enabled++){
  Dkc1WindowsSetFrameGen(enabled);if(Dkc1WindowsSavedFrameGen()!=enabled)return 22;
  Dkc1WindowsSetSquarePixels(enabled);if(Dkc1WindowsSavedSquarePixels()!=enabled)return 23;
 }
 /* Fullscreen must detach the bar without losing the menu or its state. */
 if(!Dkc1WindowsMenuBarVisible())return 10;
 Dkc1WindowsShowMenuBar(0);if(Dkc1WindowsMenuBarVisible()||GetMenu(s_window))return 11;
 Dkc1MacUpdateMenuState(0,1,1,2,3,255,0,0,0,0);
 Dkc1WindowsShowMenuBar(1);if(!Dkc1WindowsMenuBarVisible()||GetMenu(s_window)!=s_menu)return 12;
 if(!(GetMenuState(s_menu,kDkc1MacMenuFullscreen,MF_BYCOMMAND)&MF_CHECKED)||!(GetMenuState(s_menu,kDkc1MacMenuAspect16x9,MF_BYCOMMAND)&MF_CHECKED))return 13;
 Dkc1WindowsDetach();SDL_DestroyWindow(window);
 puts("WINDOWS_PLATFORM_PASS: 23 graphics fields at min/mid/max, controls roundtrip, four dark menus, scaler/color/aspect/frame-generation/pixel-aspect checkmarks, host preference roundtrips, fullscreen menu bar detach/restore");return 0;
}

/* Mac display-link entry points are not selected on Windows. */
int Dkc1MacDisplayLinkStart(void *w,double fps){(void)w;(void)fps;return 0;}
int Dkc1MacDisplayLinkWait(unsigned long long a,double b,double*c,double*d,double*e,unsigned long long*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}
void Dkc1MacDisplayLinkStop(void){}
int Dkc1MacMetalPresenterStart(void*w,double hz,Dkc1MacFullscreenScaling s,int f){(void)w;(void)hz;(void)s;(void)f;return 0;}
void Dkc1MacMetalPresenterQueueFrame(const uint32_t*p,int w,int h,int width,const Dkc1MacPresentationFrameInfo*i){(void)p;(void)w;(void)h;(void)width;(void)i;}
void Dkc1MacMetalPresenterSetGeometry(int w,int f){(void)w;(void)f;}
void Dkc1MacMetalPresenterSetScaling(Dkc1MacFullscreenScaling s){(void)s;}
void Dkc1MacMetalPresenterSetActive(int a){(void)a;}
void Dkc1MacMetalPresenterFlush(void){}
void Dkc1MacMetalPresenterStop(void){}
void Dkc1MacMetalPresenterSetGraphics(const Dkc1GraphicsSettings*s){(void)s;}

/* The HD texture experiment ships only in the Mac bundle; Windows keeps its
 * menu state, presenter hooks and persistence inert. */
void Dkc1MacConfigureHdExperiment(void){}
int Dkc1MacSavedHdTexturesEnabled(void){return 0;}
void Dkc1MacSetHdTexturesEnabled(int enabled){(void)enabled;}
void Dkc1HdMetalSetPolish(int strength){(void)strength;}
void Dkc1HdMetalSetFinish(int strength){(void)strength;}
int Dkc1MacMetalPresenterQueueHdFrame(const uint32_t *native,int width,int height,int presentation_width,const Dkc1MacPresentationFrameInfo *info){
 (void)native;(void)width;(void)height;(void)presentation_width;(void)info;return 0;
}
