#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define DKC1_PACING_LOG_TEST_HOOK
#include "win32_pacing_log.inc"

int main(void) {
  FILE *file = fopen("queued.jsonl", "wb");
  assert(file);
  HostPacingLog *log = HostPacingLogOpen(file, "queued.jsonl");
  assert(log);
  HANDLE gate = CreateEvent(NULL, TRUE, FALSE, NULL);
  log->test_gate = gate;
  /* A deliberately blocked consumer cannot block the producer. Fill the
   * bounded queue, reject excess entries, then drain in exact FIFO order. */
  for (int i = 0; i < kPacingLogSlots+17; i++)
    HostPacingLogWrite(log, "%d\n", i);
  assert(log->dropped == 17);
  SetEvent(gate);
  HostPacingLogClose(log);
  CloseHandle(gate);
  file = fopen("queued.jsonl", "rb");
  for (int i = 0; i < kPacingLogSlots; i++) {
    int value = -1;
    assert(fscanf(file, "%d", &value) == 1 && value == i);
  }
  int unexpected;
  assert(fscanf(file, "%d", &unexpected) == EOF);
  fclose(file);
  file = fopen("queued.jsonl.status.json", "rb");
  char status[512] = {0};
  assert(fread(status, 1, sizeof status-1, file) > 0);
  fclose(file);
  assert(strstr(status, "\"dropped\":17") && strstr(status, "\"io_errors\":0"));
  puts("pacing queue: PASS");
  return 0;
}
