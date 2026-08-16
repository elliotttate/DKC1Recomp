#include "cpu_trace.h"

#include <stdint.h>
#include <stdlib.h>

uint8_t g_ram[0x20000];
int snes_frame_counter = 7;

static void SetWatchEnvironment(void) {
#ifdef _WIN32
  _putenv_s("SNESRECOMP_WATCH", "0028:1");
  _putenv_s("SNESRECOMP_WATCH_LOG", "");
#else
  setenv("SNESRECOMP_WATCH", "0028:1", 1);
  unsetenv("SNESRECOMP_WATCH_LOG");
#endif
}

int main(void) {
  CpuState cpu = {0};
  SetWatchEnvironment();

  /* Prime at an explicit host boundary, then exercise nested attribution and
   * two outside-window cases.  The second outside write deliberately follows
   * DKC's joypad routine: $80:C0F8 never writes $0028, and the old entry-only
   * sampler falsely named it as the writer of each interpreter-side increment. */
  cpu_trace_func_boundary_reset(&cpu);
  cpu_trace_func_entry(&cpu, 0x111111u, "parent");
  g_ram[0x0028] = 1;
  cpu_trace_func_entry(&cpu, 0x222222u, "child");
  g_ram[0x0028] = 2;
  cpu_trace_func_exit(&cpu);
  g_ram[0x0028] = 3;
  cpu_trace_func_exit(&cpu);
  g_ram[0x0028] = 4;
  cpu_trace_func_entry(&cpu, 0x333333u, "next");
  cpu_trace_func_exit(&cpu);
  cpu_trace_func_entry(&cpu, 0x80C0F8u, "CODE_80C0F8_M0X0");
  cpu_trace_func_exit(&cpu);
  g_ram[0x0028] = 5;
  cpu_trace_func_entry(&cpu, 0x444444u, "after_interpreter_increment");
  cpu_trace_func_exit(&cpu);
  cpu_trace_func_entry(&cpu, 0x555555u, "abandoned_by_watchdog");
  g_ram[0x0028] = 6;
  cpu_trace_func_boundary_reset(&cpu);
  return 0;
}
