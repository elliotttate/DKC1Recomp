/* One immutable half-frame handoff. The worker never reads game state, the
 * producer's pixel buffers, or mutable UI globals. Keeping the half refresh
 * off the producer thread gives simulation + interpolation a full 60 Hz slot.
 *
 * Pacing follows the host pacer's mode:
 *  - waitable: the swap chain signals when the real image has been consumed;
 *    the worker presents the midpoint on the very next refresh.  The main
 *    thread collects the worker before its own wait, which fixes the order
 *    F, F+0.5, F+1 without any timer.
 *  - dwmflush: the GDI fallback waits for the next compositor pass.
 *  - timer: the free-running schedule submits half a period after the real
 *    submission (override rates, incompatible refreshes, DKC1_FRAMEGEN=force).
 */
typedef struct HostMidPresenter {
  HANDLE thread, ready, done, cancel, stop;
  HostFramePacer clock;
  HWND window;
  int width, height;
  uint8_t pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
  double target, real_submit, submit, duration_ms;
  long after_frame;
  int presented, skipped, pending, timed_out;
} HostMidPresenter;
static HostMidPresenter s_mid_presenter;

static void PresentPixelsUnlocked(HDC dc, const uint8_t *pixels,
                                  int compose_panel, UINT sync_interval);

/* Wait for the midpoint slot; returns 0 when cancelled. */
static int HostMidWait(HostMidPresenter *p) {
  HANDLE wait[2] = {p->cancel, NULL};
  switch (p->clock.mode) {
    case kHostPaceWaitable:
      wait[1] = s_d3d.waitable;
      if (!wait[1]) return 0;
      switch (WaitForMultipleObjects(2, wait, FALSE, 250)) {
        case WAIT_OBJECT_0: return 0;
        case WAIT_OBJECT_0 + 1: return 1;
        default: p->timed_out = 1; return 1;
      }
    case kHostPaceDwmFlush:
      for (int i = 0; i < p->clock.mid_flushes; i++) {
        if (WaitForSingleObject(p->cancel, 0) == WAIT_OBJECT_0) return 0;
        if (!p->clock.dwm_flush || FAILED(p->clock.dwm_flush())) {
          p->timed_out = 1;
          break;
        }
      }
      return WaitForSingleObject(p->cancel, 0) != WAIT_OBJECT_0;
    default: {
      wait[1] = p->clock.timer;
      LARGE_INTEGER now;
      for (;;) {
        if (WaitForSingleObject(p->cancel, 0) == WAIT_OBJECT_0) return 0;
        QueryPerformanceCounter(&now);
        double ms = (p->target - (double)now.QuadPart) * 1000.0 /
                    (double)p->clock.frequency.QuadPart;
        if (ms <= 0.0) return 1;
        if (ms > 1.25 && wait[1]) {
          LARGE_INTEGER due;
          due.QuadPart = -(LONGLONG)((ms - 1.0) * 10000.0);
          if (SetWaitableTimer(wait[1], &due, 0, NULL, NULL, FALSE)) {
            WaitForMultipleObjects(2, wait, FALSE, INFINITE);
            continue;
          }
        }
        YieldProcessor();
      }
    }
  }
}

static DWORD WINAPI HostMidPresentThread(void *arg) {
  HostMidPresenter *p = (HostMidPresenter *)arg;
  HANDLE wake[2] = {p->stop, p->ready};
  HANDLE mmcss = NULL;
  HostRaiseThreadPriority(&mmcss);
  for (;;) {
    if (WaitForMultipleObjects(2, wake, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
      break;
    LARGE_INTEGER now;
    p->presented = p->skipped = p->timed_out = 0;
    p->submit = p->duration_ms = 0.0;
    const int slot = HostMidWait(p);
    AcquireSRWLockExclusive(&s_present_lock);
    QueryPerformanceCounter(&now);
    if (slot && WaitForSingleObject(p->cancel, 0) != WAIT_OBJECT_0) {
      /* Timer mode: never put a stale midpoint over the next real frame
       * after a stall.  The compositor-locked modes cannot be stale: the
       * slot they waited for is by definition the next one. */
      const int fresh = p->clock.mode != kHostPaceTimer ||
          (double)now.QuadPart <
              p->target + p->clock.half_period_ticks * .75;
      if (fresh) {
        p->submit = (double)now.QuadPart;
        PresentPixelsUnlocked(NULL, p->pixels, 0, p->clock.mid_sync_interval);
        QueryPerformanceCounter(&now);
        p->duration_ms = ((double)now.QuadPart - p->submit) * 1000.0 /
                         (double)p->clock.frequency.QuadPart;
        p->presented = 1;
      }
    }
    p->skipped = !p->presented;
    ReleaseSRWLockExclusive(&s_present_lock);
    SetEvent(p->done);
  }
  HostReleaseThreadPriority(mmcss);
  return 0;
}

static void CancelMidPresent(void) {
  HostMidPresenter *p = &s_mid_presenter;
  if (!p->thread) return;
  SetEvent(p->cancel);
  WaitForSingleObject(p->done, INFINITE);
  p->pending = 0;
}

static void CloseMidPresenter(void) {
  HostMidPresenter *p = &s_mid_presenter;
  CancelMidPresent();
  if (p->thread) {
    SetEvent(p->stop);
    WaitForSingleObject(p->thread, INFINITE);
    CloseHandle(p->thread);
  }
  if (p->ready) CloseHandle(p->ready);
  if (p->done) CloseHandle(p->done);
  if (p->cancel) CloseHandle(p->cancel);
  if (p->stop) CloseHandle(p->stop);
  if (p->clock.timer) CloseHandle(p->clock.timer);
  memset(p, 0, sizeof *p);
}

static int InitMidPresenter(const HostFramePacer *clock) {
  HostMidPresenter *p = &s_mid_presenter;
  if (p->thread) return 1;
  p->clock.frequency = clock->frequency;
  p->clock.timer = CreateWaitableTimerExA(
      NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!p->clock.timer) p->clock.timer = CreateWaitableTimerA(NULL, FALSE, NULL);
  p->ready = CreateEventA(NULL, FALSE, FALSE, NULL);
  p->done = CreateEventA(NULL, TRUE, TRUE, NULL);
  p->cancel = CreateEventA(NULL, TRUE, FALSE, NULL);
  p->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (p->clock.timer && p->ready && p->done && p->cancel && p->stop)
    p->thread = CreateThread(NULL, 0, HostMidPresentThread, p, 0, NULL);
  if (!p->thread) {
    CloseMidPresenter();
    return 0;
  }
  return 1;
}

/* Wait for the worker's half-frame (if any) and fold its telemetry into
 * the pacer.  Must run before the main thread waits for its own slot so
 * the midpoint is always consumed first in compositor-locked modes. */
static void CollectMidPresent(HostFramePacer *clock) {
  HostMidPresenter *p = &s_mid_presenter;
  clock->pending_mid_presented = 0;  /* also when nothing was scheduled */
  if (!p->pending) return;
  WaitForSingleObject(p->done, INFINITE);
  clock->pending_mid_presented = p->presented;
  clock->mid_skips += p->skipped;
  clock->pending_mid_submit_tick = p->submit;
  clock->pending_mid_after_frame = p->after_frame;
  clock->pending_mid_submit_error_ms =
      p->presented && p->clock.mode == kHostPaceTimer
          ? (p->submit - p->target) * 1000.0 / (double)clock->frequency.QuadPart
          : 0.0;
  clock->pending_mid_present_ms = p->duration_ms;
  clock->pending_real_to_mid_ms = p->presented
                                      ? (p->submit - p->real_submit) * 1000.0 /
                                            (double)clock->frequency.QuadPart
                                      : 0.0;
  if (p->timed_out) clock->overruns++;
  s_present_frames_window += p->presented;
  p->pending = 0;
}

static void ScheduleMidPresent(HostFramePacer *clock, const uint8_t *pixels) {
  if (!InitMidPresenter(clock)) {
    clock->mid_skips++;
    return;
  }
  HostMidPresenter *p = &s_mid_presenter;
  p->window = s_window;
  p->width = s_width;
  p->height = s_height;
  p->clock.mode = clock->mode;
  p->clock.half_period_ticks = clock->half_period_ticks;
  p->clock.dwm_flush = clock->dwm_flush;
  p->clock.mid_flushes = clock->divisor >= 2 ? clock->divisor / 2 : 1;
  p->clock.mid_sync_interval = HostFramePacerSyncInterval(clock, 1);
  memcpy(p->pixels, pixels, (size_t)s_width * s_height * 4);
  p->target = clock->last_submit_tick + clock->half_period_ticks;
  p->real_submit = clock->pending_submit_tick;
  p->after_frame = s_host_frame;
  p->pending = 1;
  ResetEvent(p->done);
  ResetEvent(p->cancel);
  SetEvent(p->ready);
}
