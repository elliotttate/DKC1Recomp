// Qt6 settings/pause panel for the Linux desktop host. This is the first
// real UI for what linux_platform.c previously stubbed out (resume-only,
// no editing). It runs its own modal QDialog event loop -- SDL's main loop
// is fully paused for the duration, matching how the native Mac/Windows
// panels work.
//
// A QApplication is constructed lazily on first use and kept alive for the
// process lifetime; SDL owns its own window independently, and the two
// toolkits coexist fine as long as only one has a modal loop running at a
// time (true here: SDL's loop is paused whenever this dialog is open).
#include "linux_qt_dialog.h"

extern "C" {
#include "desktop_crt.h"
#include "macos_pause_menu.h" /* Dkc1MacSaveGraphics, Dkc1MacApplyGraphics */
#include "macos_controls.h"    /* Dkc1MacSaveControls */
#include "linux_presenter.h"   /* Dkc1LinuxPresenterActiveBackend */
}

#include <QApplication>
#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QKeyEvent>
#include <QWindow>

#include <array>
#include <cstring>

namespace {

// SDL_Scancode values are the USB HID usage-page-7 table, not ASCII --
// this table covers the keys someone would realistically bind to a
// gameplay action. Left/Right modifier variants (Ctrl/Shift/Alt/Super)
// collapse to the Left scancode: Qt's cross-platform QKeyEvent::key()
// doesn't reliably distinguish the two without digging into native
// scancodes, and games conventionally treat L/R the same anyway.
int QtKeyToSdlScancode(int qt_key) {
  switch (qt_key) {
    case Qt::Key_A: return 4;   case Qt::Key_B: return 5;
    case Qt::Key_C: return 6;   case Qt::Key_D: return 7;
    case Qt::Key_E: return 8;   case Qt::Key_F: return 9;
    case Qt::Key_G: return 10;  case Qt::Key_H: return 11;
    case Qt::Key_I: return 12;  case Qt::Key_J: return 13;
    case Qt::Key_K: return 14;  case Qt::Key_L: return 15;
    case Qt::Key_M: return 16;  case Qt::Key_N: return 17;
    case Qt::Key_O: return 18;  case Qt::Key_P: return 19;
    case Qt::Key_Q: return 20;  case Qt::Key_R: return 21;
    case Qt::Key_S: return 22;  case Qt::Key_T: return 23;
    case Qt::Key_U: return 24;  case Qt::Key_V: return 25;
    case Qt::Key_W: return 26;  case Qt::Key_X: return 27;
    case Qt::Key_Y: return 28;  case Qt::Key_Z: return 29;
    case Qt::Key_1: return 30;  case Qt::Key_2: return 31;
    case Qt::Key_3: return 32;  case Qt::Key_4: return 33;
    case Qt::Key_5: return 34;  case Qt::Key_6: return 35;
    case Qt::Key_7: return 36;  case Qt::Key_8: return 37;
    case Qt::Key_9: return 38;  case Qt::Key_0: return 39;
    case Qt::Key_Return: return 40;
    case Qt::Key_Escape: return 41;
    case Qt::Key_Backspace: return 42;
    case Qt::Key_Tab: return 43;
    case Qt::Key_Space: return 44;
    case Qt::Key_Minus: return 45;
    case Qt::Key_Equal: return 46;
    case Qt::Key_BracketLeft: return 47;
    case Qt::Key_BracketRight: return 48;
    case Qt::Key_Backslash: return 49;
    case Qt::Key_Semicolon: return 51;
    case Qt::Key_Apostrophe: return 52;
    case Qt::Key_QuoteLeft: return 53;
    case Qt::Key_Comma: return 54;
    case Qt::Key_Period: return 55;
    case Qt::Key_Slash: return 56;
    case Qt::Key_CapsLock: return 57;
    case Qt::Key_F1: return 58;   case Qt::Key_F2: return 59;
    case Qt::Key_F3: return 60;   case Qt::Key_F4: return 61;
    case Qt::Key_F5: return 62;   case Qt::Key_F6: return 63;
    case Qt::Key_F7: return 64;   case Qt::Key_F8: return 65;
    case Qt::Key_F9: return 66;   case Qt::Key_F10: return 67;
    case Qt::Key_F11: return 68;  case Qt::Key_F12: return 69;
    case Qt::Key_Print: return 70;
    case Qt::Key_ScrollLock: return 71;
    case Qt::Key_Pause: return 72;
    case Qt::Key_Insert: return 73;
    case Qt::Key_Home: return 74;
    case Qt::Key_PageUp: return 75;
    case Qt::Key_Delete: return 76;
    case Qt::Key_End: return 77;
    case Qt::Key_PageDown: return 78;
    case Qt::Key_Right: return 79;
    case Qt::Key_Left: return 80;
    case Qt::Key_Down: return 81;
    case Qt::Key_Up: return 82;
    case Qt::Key_NumLock: return 83;
    case Qt::Key_Control: return 224;
    case Qt::Key_Shift: return 225;
    case Qt::Key_Alt: return 226;
    case Qt::Key_Meta:
    case Qt::Key_Super_L: return 227;
    default: return -1;
  }
}

// Inverse of the above, for showing the currently-bound key's name.
const char *SdlScancodeToLabel(int scancode) {
  static const std::pair<int, const char *> table[] = {
    {4,"A"},{5,"B"},{6,"C"},{7,"D"},{8,"E"},{9,"F"},{10,"G"},{11,"H"},
    {12,"I"},{13,"J"},{14,"K"},{15,"L"},{16,"M"},{17,"N"},{18,"O"},{19,"P"},
    {20,"Q"},{21,"R"},{22,"S"},{23,"T"},{24,"U"},{25,"V"},{26,"W"},{27,"X"},
    {28,"Y"},{29,"Z"},{30,"1"},{31,"2"},{32,"3"},{33,"4"},{34,"5"},{35,"6"},
    {36,"7"},{37,"8"},{38,"9"},{39,"0"},{40,"Enter"},{41,"Esc"},
    {42,"Backspace"},{43,"Tab"},{44,"Space"},{45,"-"},{46,"="},{47,"["},
    {48,"]"},{49,"\\"},{51,";"},{52,"'"},{53,"`"},{54,","},{55,"."},{56,"/"},
    {57,"CapsLock"},{58,"F1"},{59,"F2"},{60,"F3"},{61,"F4"},{62,"F5"},
    {63,"F6"},{64,"F7"},{65,"F8"},{66,"F9"},{67,"F10"},{68,"F11"},
    {69,"F12"},{70,"PrintScreen"},{71,"ScrollLock"},{72,"Pause"},
    {73,"Insert"},{74,"Home"},{75,"PageUp"},{76,"Delete"},{77,"End"},
    {78,"PageDown"},{79,"Right"},{80,"Left"},{81,"Down"},{82,"Up"},
    {83,"NumLock"},{224,"Ctrl"},{225,"Shift"},{226,"Alt"},{227,"Super"},
    {228,"RCtrl"},{229,"RShift"},{230,"RAlt"},{231,"RSuper"},
  };
  for (const auto &entry : table)
    if (entry.first == scancode) return entry.second;
  return "?";
}

// A button that shows the currently-bound key and captures the next
// keypress when clicked, writing the resulting SDL scancode into *slot.
class KeyBindButton : public QPushButton {
 public:
  KeyBindButton(int *slot, QWidget *parent = nullptr)
      : QPushButton(parent), slot_(slot) {
    Refresh();
    connect(this, &QPushButton::clicked, this, &KeyBindButton::StartCapture);
  }

 protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (!capturing_) { QPushButton::keyPressEvent(event); return; }
    int scancode = QtKeyToSdlScancode(event->key());
    if (scancode >= 0) *slot_ = scancode;
    capturing_ = false;
    Refresh();
  }
  void focusOutEvent(QFocusEvent *event) override {
    if (capturing_) { capturing_ = false; Refresh(); }
    QPushButton::focusOutEvent(event);
  }

 private:
  void StartCapture() {
    capturing_ = true;
    setText(QStringLiteral("Press a key..."));
    setFocus(Qt::OtherFocusReason);
  }
  void Refresh() {
    setText(QString::fromUtf8(SdlScancodeToLabel(*slot_)));
  }
  int *slot_;
  bool capturing_ = false;
};

class SettingsDialog : public QDialog {
 public:
  SettingsDialog(Dkc1GraphicsSettings *gfx, Dkc1Controls *controls,
                 int initial_page, QWidget *parent = nullptr)
      : QDialog(parent), gfx_(gfx), controls_(controls) {
    setWindowTitle(QStringLiteral("DKC1Recomp Settings"));
    setModal(true);
    resize(560, 420);

    auto *layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);
    tabs_->addTab(BuildGraphicsTab(), QStringLiteral("Graphics"));
    tabs_->addTab(BuildControlsTab(), QStringLiteral("Controls"));
    tabs_->addTab(BuildAssistTab(), QStringLiteral("Assist"));
    if (initial_page == 4) tabs_->setCurrentIndex(1);

    auto *buttons = new QHBoxLayout();
    auto *quit_button = new QPushButton(QStringLiteral("Quit Game"), this);
    auto *resume_button = new QPushButton(QStringLiteral("Resume"), this);
    resume_button->setDefault(true);
    buttons->addWidget(quit_button);
    buttons->addStretch();
    buttons->addWidget(resume_button);
    layout->addLayout(buttons);

    connect(resume_button, &QPushButton::clicked, this, &QDialog::accept);
    connect(quit_button, &QPushButton::clicked, this, [this] {
      quit_requested_ = true;
      accept();
    });
  }

  bool QuitRequested() const { return quit_requested_; }

 private:
  QWidget *BuildGraphicsTab() {
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    display_ = new QComboBox(page);
    display_->addItems({"Flat", "CRT"});
    display_->setCurrentIndex(gfx_->display);
    form->addRow("Display", display_);
    if (Dkc1LinuxPresenterActiveBackend() == 1 /* Vulkan */) {
      // The Vulkan renderer only implements the flat/sharp-bilinear
      // present pass so far -- CRT is real on the OpenGL/Metal/Windows
      // backends but selecting it here currently just renders through
      // the same flat pass on Vulkan (it doesn't fail, it just doesn't
      // do anything visually different yet).
      auto *note = new QLabel(
          "Vulkan renderer: CRT isn't implemented yet, only Flat/Sharp "
          "Bilinear render. Set DKC1_DISABLE_VULKAN=1 for the OpenGL "
          "fallback's full CRT pipeline.", page);
      note->setWordWrap(true);
      form->addRow(QString(), note);
    }

    upscaler_ = new QComboBox(page);
    upscaler_->addItems({"Nearest", "Bilinear", "Reconstruct", "Sharp Bilinear"});
    upscaler_->setCurrentIndex(gfx_->upscaler);
    form->addRow("Upscaler", upscaler_);

    aspect_ = new QComboBox(page);
    aspect_->addItems({"Native", "16:10", "16:9"});
    aspect_->setCurrentIndex(gfx_->aspect);
    form->addRow("Aspect", aspect_);

    window_scale_ = new QSpinBox(page);
    window_scale_->setRange(1, 4);
    window_scale_->setValue(gfx_->window_scale > 0 ? gfx_->window_scale : 3);
    form->addRow("Window scale", window_scale_);

    fullscreen_ = new QCheckBox("Fullscreen", page);
    fullscreen_->setChecked(gfx_->fullscreen != 0);
    form->addRow(QString(), fullscreen_);

    audio_enabled_ = new QCheckBox("Audio enabled", page);
    audio_enabled_->setChecked(gfx_->audio_enabled != 0);
    form->addRow(QString(), audio_enabled_);

    volume_ = new QSlider(Qt::Horizontal, page);
    volume_->setRange(0, 100);
    volume_->setValue(gfx_->volume);
    form->addRow("Volume", volume_);

    // CRT tuning, shown regardless of Display so it's there when you flip
    // to CRT rather than needing a second trip through the dialog.
    auto *crt_box = new QGroupBox("CRT", page);
    auto *crt_form = new QFormLayout(crt_box);
    crt_preset_ = new QComboBox(crt_box);
    crt_preset_->addItems({"Living Room", "Studio", "Soft", "Custom"});
    crt_preset_->setCurrentIndex(gfx_->crt.preset);
    crt_form->addRow("Preset", crt_preset_);
    crt_scanlines_ = MakeCrtSlider(crt_box, crt_form, "Scanlines", gfx_->crt.scanlines);
    crt_sharpness_ = MakeCrtSlider(crt_box, crt_form, "Sharpness", gfx_->crt.sharpness);
    crt_mask_strength_ = MakeCrtSlider(crt_box, crt_form, "Mask strength", gfx_->crt.mask_strength);
    crt_glow_ = MakeCrtSlider(crt_box, crt_form, "Glow", gfx_->crt.glow);
    crt_halation_ = MakeCrtSlider(crt_box, crt_form, "Halation", gfx_->crt.halation);
    crt_curvature_ = MakeCrtSlider(crt_box, crt_form, "Curvature", gfx_->crt.curvature);
    connect(crt_preset_, &QComboBox::currentIndexChanged, this, [this](int index) {
      // "Custom" (index 3) means "keep whatever the sliders currently
      // say" -- Dkc1CrtSettingsApplyPreset only defines the other three
      // and rejects that index outright, so leave the sliders alone then.
      Dkc1CrtSettings preview = gfx_->crt;
      if (!Dkc1CrtSettingsApplyPreset(&preview, index)) return;
      crt_scanlines_->setValue(preview.scanlines);
      crt_sharpness_->setValue(preview.sharpness);
      crt_mask_strength_->setValue(preview.mask_strength);
      crt_glow_->setValue(preview.glow);
      crt_halation_->setValue(preview.halation);
      crt_curvature_->setValue(preview.curvature);
    });
    form->addRow(crt_box);

    return page;
  }

  QSlider *MakeCrtSlider(QWidget *parent, QFormLayout *form,
                         const char *label, int value) {
    auto *slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(0, 100);
    slider->setValue(value);
    form->addRow(label, slider);
    return slider;
  }

  QWidget *BuildControlsTab() {
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);

    static const char *kActionNames[12] = {
        "Up", "Down", "Left", "Right", "A", "B",
        "X", "Y", "L", "R", "Start", "Select"};

    auto *grid_box = new QGroupBox("Player 1 keyboard bindings", page);
    auto *grid = new QGridLayout(grid_box);
    for (int i = 0; i < 12; i++) {
      grid->addWidget(new QLabel(kActionNames[i], grid_box), i / 2,
                       (i % 2) * 2);
      auto *button = new KeyBindButton(&controls_->keys[0][i], grid_box);
      key_buttons_[i] = button;
      grid->addWidget(button, i / 2, (i % 2) * 2 + 1);
    }
    outer->addWidget(grid_box);

    auto *form = new QFormLayout();
    deadzone_ = new QSlider(Qt::Horizontal, page);
    deadzone_->setRange(0, 100);
    deadzone_->setValue(controls_->deadzone[0]);
    form->addRow("Gamepad deadzone", deadzone_);
    outer->addLayout(form);

    outer->addWidget(new QLabel(
        "Gamepad button bindings and Player 2 use their existing defaults "
        "for now -- edit them by hand in linux.ini if needed.", page));
    outer->addStretch();
    return page;
  }

  QWidget *BuildAssistTab() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    assist_enabled_ = new QCheckBox(
        "Enable Assist (rewind / fast-forward / save-state shortcuts)", page);
    assist_enabled_->setChecked(controls_->assist_enabled != 0);
    layout->addWidget(assist_enabled_);
    layout->addStretch();
    return page;
  }

 public:
  void WriteBack() {
    gfx_->display = display_->currentIndex();
    gfx_->upscaler = upscaler_->currentIndex();
    gfx_->aspect = aspect_->currentIndex();
    gfx_->window_scale = window_scale_->value();
    gfx_->fullscreen = fullscreen_->isChecked() ? 1 : 0;
    gfx_->audio_enabled = audio_enabled_->isChecked() ? 1 : 0;
    gfx_->volume = volume_->value();
    gfx_->crt.preset = crt_preset_->currentIndex();
    gfx_->crt.scanlines = crt_scanlines_->value();
    gfx_->crt.sharpness = crt_sharpness_->value();
    gfx_->crt.mask_strength = crt_mask_strength_->value();
    gfx_->crt.glow = crt_glow_->value();
    gfx_->crt.halation = crt_halation_->value();
    gfx_->crt.curvature = crt_curvature_->value();
    Dkc1GraphicsClamp(gfx_);

    controls_->deadzone[0] = deadzone_->value();
    controls_->assist_enabled = assist_enabled_->isChecked() ? 1 : 0;
    // key_buttons_ already wrote directly into controls_->keys[0][i] as
    // each binding was captured; nothing further to copy for those.
  }

 private:
  Dkc1GraphicsSettings *gfx_;
  Dkc1Controls *controls_;
  QTabWidget *tabs_;
  QComboBox *display_, *upscaler_, *aspect_, *crt_preset_;
  QSpinBox *window_scale_;
  QCheckBox *fullscreen_, *audio_enabled_, *assist_enabled_;
  QSlider *volume_, *deadzone_;
  QSlider *crt_scanlines_, *crt_sharpness_, *crt_mask_strength_,
          *crt_glow_, *crt_halation_, *crt_curvature_;
  KeyBindButton *key_buttons_[12] = {};
  bool quit_requested_ = false;
};

}  // namespace

extern "C" int Dkc1LinuxQtShowDialog(void *native_window,
                                     Dkc1GraphicsSettings *settings,
                                     Dkc1Controls *controls,
                                     int graphics_page) {
  static QApplication *app = nullptr;
  if (!app) {
    static int argc = 1;
    static char prog_name[] = "DKC1Recomp";
    static char *argv[] = {prog_name, nullptr};
    app = new QApplication(argc, argv);
  }

  SettingsDialog dialog(settings, controls, graphics_page);
  if (SDL_Window *sdl_window = static_cast<SDL_Window *>(native_window)) {
    int win_x, win_y, win_w, win_h;
    SDL_GetWindowPosition(sdl_window, &win_x, &win_y);
    SDL_GetWindowSize(sdl_window, &win_w, &win_h);
    dialog.move(win_x + (win_w - dialog.width()) / 2,
                win_y + (win_h - dialog.height()) / 2);
  }
  int result = dialog.exec();
  if (result != QDialog::Accepted) {
    // Closed via the title-bar X (or Escape on a non-capturing widget):
    // discard whatever was mid-edit and tell the caller to preserve
    // whatever pause state it already had, same as the other platforms'
    // panels do on a plain dismiss.
    return 0;
  }

  dialog.WriteBack();
  // Dkc1MacApplyGraphics clamps, applies fullscreen/aspect/edge changes,
  // persists to disk, AND pushes the new values into the presenter's own
  // settings copy (linux_presenter.c keeps one separate from sdl_host.c's
  // live struct) -- without this last part the renderer kept using
  // whatever was active at startup even though the struct itself had
  // already changed.
  Dkc1MacApplyGraphics(settings);
  Dkc1MacSaveControls(controls);

  if (dialog.QuitRequested()) {
    // The pause-menu contract only has resume/keep-paused; signal quit by
    // way of an immediate clean process exit rather than stretching that
    // contract. Settings/controls are already persisted above.
    std::exit(0);
  }
  return 1;  // resume
}
