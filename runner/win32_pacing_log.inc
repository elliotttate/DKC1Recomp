/* Single-producer/single-consumer diagnostic queue. The frame thread only
 * formats into preallocated memory; the worker alone performs file I/O.
 * A blocked disk drops diagnostic records instead of blocking gameplay.
 * The sidecar makes any incomplete/error trace fail its verification gate. */
#include <stdarg.h>

enum { kPacingLogSlots = 8192, kPacingLogLine = 4096 };
typedef struct HostPacingLogSlot {
  unsigned length;
  char text[kPacingLogLine];
} HostPacingLogSlot;
typedef struct HostPacingLog {
  FILE *file;
  HANDLE ready, thread;
  volatile LONG head, tail, stopping;
  unsigned dropped, errors, written;
  char status_path[1200];
#ifdef DKC1_PACING_LOG_TEST_HOOK
  HANDLE test_gate;
#endif
  HostPacingLogSlot slots[kPacingLogSlots];
} HostPacingLog;

static DWORD WINAPI HostPacingLogWorker(void *arg) {
  HostPacingLog *log = (HostPacingLog *)arg;
  for (;;) {
    WaitForSingleObject(log->ready, INFINITE);
#ifdef DKC1_PACING_LOG_TEST_HOOK
    if (log->test_gate) WaitForSingleObject(log->test_gate, INFINITE);
#endif
    unsigned tail = (unsigned)InterlockedCompareExchange(&log->tail, 0, 0);
    while (tail != (unsigned)InterlockedCompareExchange(&log->head, 0, 0)) {
      const HostPacingLogSlot *slot = &log->slots[tail % kPacingLogSlots];
      if (fwrite(slot->text, 1, slot->length, log->file) != slot->length)
        log->errors++;
      log->written++;
      InterlockedExchange(&log->tail, (LONG)++tail);
    }
    if (InterlockedCompareExchange(&log->stopping, 0, 0)) break;
  }
  if (fflush(log->file)) log->errors++;
  return 0;
}

static HostPacingLog *HostPacingLogOpen(FILE *file, const char *path) {
  HostPacingLog *log = (HostPacingLog *)calloc(1, sizeof *log);
  if (!log) return NULL;
  log->file = file;
  snprintf(log->status_path, sizeof log->status_path, "%s.status.json", path);
  /* Touch every page before gameplay, rather than faulting it in later. */
  for (size_t at = 0; at < sizeof log->slots; at += 4096)
    ((volatile char *)log->slots)[at] = 0;
  log->ready = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (log->ready)
    log->thread = CreateThread(NULL, 0, HostPacingLogWorker, log, 0, NULL);
  if (!log->thread) {
    if (log->ready) CloseHandle(log->ready);
    free(log);
    return NULL;
  }
  return log;
}

static void HostPacingLogWrite(HostPacingLog *log, const char *format, ...) {
  unsigned head = (unsigned)InterlockedCompareExchange(&log->head, 0, 0);
  unsigned tail = (unsigned)InterlockedCompareExchange(&log->tail, 0, 0);
  if (head - tail == kPacingLogSlots) { log->dropped++; return; }
  HostPacingLogSlot *slot = &log->slots[head % kPacingLogSlots];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(slot->text, sizeof slot->text, format, args);
  va_end(args);
  if (length < 0 || length >= (int)sizeof slot->text) {
    log->dropped++;
    return;
  }
  slot->length = (unsigned)length;
  InterlockedExchange(&log->head, (LONG)(head + 1));
  SetEvent(log->ready);
}

static void HostPacingLogClose(HostPacingLog *log) {
  InterlockedExchange(&log->stopping, 1);
  SetEvent(log->ready);
  WaitForSingleObject(log->thread, INFINITE);
  if (fclose(log->file)) log->errors++;
  FILE *status = fopen(log->status_path, "wb");
  if (status) {
    fprintf(status, "{\"schema\":\"dkc1.pacing-log-status.v1\","
                    "\"records\":%u,\"dropped\":%u,\"io_errors\":%u}\n",
            log->written, log->dropped, log->errors);
    fclose(status);
  }
  if (!status || log->dropped || log->errors)
    fprintf(stderr, "[pacing] incomplete diagnostic log: dropped=%u errors=%u status=%d\n",
            log->dropped, log->errors, status != NULL);
  CloseHandle(log->thread);
  CloseHandle(log->ready);
  free(log);
}
