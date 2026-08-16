/* DKC1_REPLACE demo: readable replacement for CODE_BDF88A_M0X0.
 *
 * CODE_BDF88A computes the object-scanner window from the camera:
 *   window_left  ($00EF) = camera_x - cull_left, clamped to 0 when the
 *                          subtraction wraps "negative" (>= $FC00)
 *   window_right ($00F1) = window_left + scan_span
 * cull_left/scan_span go through the widescreen adapters
 * (Dkc1VideoObjectScannerCullLeft/CullSpan) exactly like the generated
 * original, so this replacement is presentation-policy-transparent.
 *
 * Contract (docs/MOD_LAYER.md): the build takes over the generated
 * variant symbol only after tools/gen_replacements.py verifies the
 * original ROM bytes for the region; DKC1_REPLACE_DISABLE=1 falls back
 * to the untouched original (CODE_BDF88A_M0X0_original) at runtime.
 * Equivalence is proven per route with the differential oracle:
 *   python tools/oracle_run.py CODE_BDF88A --exe <stock trace exe> ...
 *   python tools/oracle_run.py CODE_BDF88A --exe <replace trace exe> ...
 *   python tools/oracle_diff.py stock.jsonl replaced.jsonl  -> IDENTICAL
 * Cycle accounting, flag semantics (including decimal mode), and the
 * RTS return machinery are replicated exactly — a replacement that
 * drifts timing desynchronizes the deterministic run even when its
 * state effects are right.
 *
 * The PROLOGUE/EPILOGUE sections are verbatim engine machinery copied
 * from the generated original (candidate for a future wrapper
 * generator); the READABLE CORE between them is the hand-written part.
 */

#include <stdio.h>
#include <stdlib.h>

#include "cpu_state.h"
#include "cpu_trace.h"
#include "common_cpu_infra.h"
#include "funcs.h"
#include "dkc1_video.h"

/* the untouched generated original (renamed by the build override) */
RecompReturn CODE_BDF88A_M0X0_original(CpuState *cpu);

static int dkc1_replace_disabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("DKC1_REPLACE_DISABLE");
        cached = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return cached;
}

RecompReturn CODE_BDF88A_M0X0(CpuState *cpu) {
  if (dkc1_replace_disabled())
    return CODE_BDF88A_M0X0_original(cpu);

  /* ---- PROLOGUE (verbatim engine machinery) ---- */
  extern const char *g_last_recomp_func;
  g_last_recomp_func = "CODE_BDF88A_M0X0";
  RecompStackPush("CODE_BDF88A_M0X0");
  cpu_dbg_funcname("CODE_BDF88A_M0X0");
  cpu_trace_func_entry(cpu, 0xBDF88A, "CODE_BDF88A_M0X0");
  if (interp_bridge_lle_master_deadline_reached(cpu)) {
    RecompStackPop();
    return interp_bridge_lle_yield_unwind(cpu, 0xBDF88Au);
  }
  uint16 _entry_s = cpu->S;
  uint8 _hrv = cpu->host_return_valid;
  if (cpu_take_tailcall_return_context(&_entry_s, &_hrv)) {
    cpu->host_return_valid = _hrv;
  }
  uint32 _host_return_pc24 = 0xFFFFFFFFu;
  if (_hrv == 2 || _hrv == 3) {
    uint16 _host_rpcl = cpu_read8(cpu, 0x00, (uint16)(_entry_s + 1u));
    uint16 _host_rpch = cpu_read8(cpu, 0x00, (uint16)(_entry_s + 2u));
    uint8 _host_rpb = (_hrv == 3)
        ? cpu_read8(cpu, 0x00, (uint16)(_entry_s + 3u)) : cpu->PB;
    _host_return_pc24 = ((uint32)_host_rpb << 16) |
        (uint16)((((_host_rpch << 8) | _host_rpcl) + 1u) & 0xFFFFu);
  }
  if (g_recomp_stack_top >= 1)
    g_cpu_entry_s[g_recomp_stack_top - 1] = _entry_s;

  /* ---- READABLE CORE -------------------------------------------- */
  /* Block $BDF88A: LDA CameraX; SEC; SBC #cull_left; CMP #$FC00; BCC */
  cpu_trace_block(cpu, 0xBDF88A);
  WatchdogCheck();
  if (interp_bridge_lle_master_deadline_reached(cpu)) {
    RecompStackPop();
    return interp_bridge_lle_yield_unwind(cpu, 0xBDF88Au);
  }
  cpu->coprocessor_master_cycles = cpu->master_cycles;
  cpu->cycles += 15;
  cpu->master_cycles += 15 * (g_memsel ? 6 : 8);

  /* A = CameraX ($088B via DB) */
  uint16 camera_x = cpu_read16(cpu, cpu->DB, (uint16)(0x088b));
  cpu_write_a_m(cpu, camera_x);
  cpu->_flag_Z = (camera_x == 0) ? 1 : 0;
  cpu->_flag_N = ((camera_x & 0x8000) != 0) ? 1 : 0;
  cpu->P = (uint8)((cpu->P & ~0x82) | (cpu->_flag_Z ? 0x02 : 0) |
                   (cpu->_flag_N ? 0x80 : 0));
  /* SEC */
  cpu->_flag_C = 1;
  cpu->P = (uint8)(cpu->P | 0x01);
  /* SBC #cull_left — the widescreen adapter widens the left cull */
  {
    uint16 _v2 = Dkc1VideoObjectScannerCullLeft(0x20);
    uint16 _v3 = cpu_read_a16(cpu);
    uint16 window_left;
    if (cpu->_flag_D) {
      /* decimal-mode SBC, exact BCD semantics as generated */
      int _bcv = ((_v2 & 0xFFFF) ^ 0xffff) & 0xffff;
      int _bcd = ((_v3 & 0xFFFF) & 0xf) + (_bcv & 0xf) + cpu->_flag_C;
      if (_bcd < 0x10) _bcd = (_bcd - 0x6) & ((_bcd - 0x6 < 0) ? 0xf : 0x1f);
      _bcd = ((_v3 & 0xFFFF) & 0xf0) + (_bcv & 0xf0) + _bcd;
      if (_bcd < 0x100) _bcd = (_bcd - 0x60) & ((_bcd - 0x60 < 0) ? 0xff : 0x1ff);
      _bcd = ((_v3 & 0xFFFF) & 0xf00) + (_bcv & 0xf00) + _bcd;
      if (_bcd < 0x1000) _bcd = (_bcd - 0x600) & ((_bcd - 0x600 < 0) ? 0xfff : 0x1fff);
      _bcd = ((_v3 & 0xFFFF) & 0xf000) + (_bcv & 0xf000) + _bcd;
      cpu->_flag_V = (((_v3 & 0xFFFF) & 0x8000) == (_bcv & 0x8000)) &&
                     ((_bcv & 0x8000) != (_bcd & 0x8000)) ? 1 : 0;
      if (_bcd < 0x10000) _bcd -= 0x6000;
      cpu->_flag_C = (_bcd > 0xffff) ? 1 : 0;
      window_left = (uint16)_bcd;
    } else {
      uint32 _t = (uint32)_v3 - (uint32)_v2 - (1 - cpu->_flag_C);
      window_left = (uint16)_t;
      cpu->_flag_C = (_t & 0x10000) ? 0 : 1;
      cpu->_flag_V = (((_v3 ^ _v2) & (_v3 ^ window_left) & 0x8000) != 0)
          ? 1 : 0;
    }
    cpu->_flag_Z = (window_left == 0) ? 1 : 0;
    cpu->_flag_N = ((window_left & 0x8000) != 0) ? 1 : 0;
    cpu_write_a_m(cpu, window_left);

    /* CMP #$FC00: "did the subtraction wrap negative?" */
    uint16 _v5 = 0xfc00;
    uint16 _v6 = cpu_read_a16(cpu);
    uint32 _tc = (uint32)_v6 - (uint32)_v5;
    cpu->_flag_C = (_v6 >= _v5) ? 1 : 0;
    cpu->_flag_Z = (((uint16)_tc) == 0) ? 1 : 0;
    cpu->_flag_N = ((((uint16)_tc) & 0x8000) != 0) ? 1 : 0;
  }
  if (cpu->_flag_C == 0) {
    /* window_left < $FC00: keep it (branch taken, +1 cycle) */
    cpu->cycles += 1;
    cpu->master_cycles += (g_memsel ? 6 : 8);
    goto L_F899;
  }

  /* Block $BDF896: clamp the wrapped-negative window_left to 0 */
  cpu_trace_block(cpu, 0xBDF896);
  WatchdogCheck();
  if (interp_bridge_lle_master_deadline_reached(cpu)) {
    RecompStackPop();
    return interp_bridge_lle_yield_unwind(cpu, 0xBDF896u);
  }
  cpu->coprocessor_master_cycles = cpu->master_cycles;
  cpu->cycles += 3;
  cpu->master_cycles += 3 * (g_memsel ? 6 : 8);
  cpu_write_a_m(cpu, 0);
  cpu->_flag_Z = 1;
  cpu->_flag_N = 0;
  cpu->P = (uint8)((cpu->P & ~0x82) | 0x02);

L_F899:
  /* Block $BDF899: store window; right edge = left + span; RTS */
  cpu_trace_block(cpu, 0xBDF899);
  WatchdogCheck();
  if (interp_bridge_lle_master_deadline_reached(cpu)) {
    RecompStackPop();
    return interp_bridge_lle_yield_unwind(cpu, 0xBDF899u);
  }
  cpu->coprocessor_master_cycles = cpu->master_cycles;
  cpu->cycles += 19;
  cpu->master_cycles += 19 * (g_memsel ? 6 : 8);
  if (cpu->D & 0xFF) {
    cpu->cycles += 1;
    cpu->master_cycles += (g_memsel ? 6 : 8);
  }
  /* $00EF = window_left (dp store) */
  cpu_write16(cpu, 0x00, (uint16)(cpu->D + 0x00ef), cpu_read_a16(cpu));
  /* CLC; ADC #scan_span — adapter widens the span for widescreen */
  cpu->_flag_C = 0;
  cpu->P = (uint8)(cpu->P & ~0x01);
  {
    uint16 scan_span = Dkc1VideoObjectScannerCullSpan(0x140);
    uint16 left = cpu_read_a16(cpu);
    uint16 window_right;
    if (cpu->_flag_D) {
      /* decimal-mode ADC, exact BCD semantics as generated */
      int _bcd = (left & 0xf) + (scan_span & 0xf) + cpu->_flag_C;
      if (_bcd > 0x9) _bcd = ((_bcd + 0x6) & 0xf) + 0x10;
      _bcd = (left & 0xf0) + (scan_span & 0xf0) + _bcd;
      if (_bcd > 0x9f) _bcd = ((_bcd + 0x60) & 0xff) + 0x100;
      _bcd = (left & 0xf00) + (scan_span & 0xf00) + _bcd;
      if (_bcd > 0x9ff) _bcd = ((_bcd + 0x600) & 0xfff) + 0x1000;
      _bcd = (left & 0xf000) + (scan_span & 0xf000) + _bcd;
      cpu->_flag_V = ((left & 0x8000) == (scan_span & 0x8000)) &&
                     ((scan_span & 0x8000) != (_bcd & 0x8000)) ? 1 : 0;
      if (_bcd > 0x9fff) _bcd += 0x6000;
      cpu->_flag_C = (_bcd > 0xffff) ? 1 : 0;
      window_right = (uint16)_bcd;
    } else {
      uint32 _t = (uint32)left + (uint32)scan_span + cpu->_flag_C;
      window_right = (uint16)_t;
      cpu->_flag_C = (_t & 0x10000) ? 1 : 0;
      cpu->_flag_V = (((left ^ window_right) &
                       (scan_span ^ window_right) & 0x8000) != 0) ? 1 : 0;
    }
    cpu->_flag_Z = (window_right == 0) ? 1 : 0;
    cpu->_flag_N = ((window_right & 0x8000) != 0) ? 1 : 0;
    cpu_write_a_m(cpu, window_right);
  }
  if (cpu->D & 0xFF) {
    cpu->cycles += 1;
    cpu->master_cycles += (g_memsel ? 6 : 8);
  }
  /* $00F1 = window_right (dp store) */
  cpu_write16(cpu, 0x00, (uint16)(cpu->D + 0x00f1), cpu_read_a16(cpu));

  /* ---- EPILOGUE: RTS return machinery (verbatim) ---- */
  { uint16 _ret_s = cpu->S;
    cpu->S = (uint16)(cpu->S + 1);
    uint16 _rpcl = (uint16)cpu_read8(cpu, 0x00, cpu->S);
    cpu->S = (uint16)(cpu->S + 1);
    uint16 _rpch = (uint16)cpu_read8(cpu, 0x00, cpu->S);
    uint8 _rpb = cpu->PB;
    uint32 _rpc = (uint32)((((_rpch << 8) | _rpcl) + 1) & 0xFFFFu);
    uint32 _rpc24 = ((uint32)_rpb << 16) | _rpc;
    if (_hrv == 2 && _ret_s == _entry_s &&
        _rpc24 != _host_return_pc24 && !cpu_dispatch_has_entry(cpu, _rpc24)) {
      RecompStackPop();
      return interp_tier_dispatch_rewritten_return(cpu, _rpc24, 0xbdf8a1u);
    }
    if (_hrv == 2 && _ret_s == _entry_s && _rpc24 == _host_return_pc24) {
      RecompStackPop();
      return RECOMP_RETURN_NORMAL;  /* RTS host return */ }
    if (_ret_s != _entry_s) {
      int _anc_skip = cpu_resolve_ancestor_skip(_ret_s);
      if (_anc_skip >= 0) {
        cpu_trace_mark_nlr_exit(BD_EXIT_KIND_TRAMPOLINE);
        RecompStackPop();
        return (RecompReturn)_anc_skip;  /* RTS return-to-ancestor */ }
      if (interp_bridge_return_targets_owner(_ret_s, cpu->S)) {
        cpu_trace_mark_nlr_exit(BD_EXIT_KIND_TRAMPOLINE);
        RecompStackPop();
        return interp_bridge_lle_yield_unwind(cpu, _rpc24);
      }
      if ((uint16)(_ret_s - _entry_s) < 0x8000u &&
          interp_bridge_has_direct_paired_bounce()) {
        RecompStackPop();
        return interp_tier_dispatch_rewritten_return(cpu, _rpc24, 0xbdf8a1u); }
    }
    if (_ret_s != _entry_s && cpu->S == _entry_s &&
        interp_bridge_has_direct_paired_bounce()) {
      RecompStackPop();
      return interp_tier_dispatch_rewritten_return(cpu, _rpc24, 0xbdf8a1u);
    }
    if (_ret_s != _entry_s &&
        (uint16)(_entry_s - _ret_s) < 0x8000u &&
        cpu->S != _entry_s &&
        (uint16)(cpu->S - _entry_s) < 0x8000u) {
      RecompStackPop();
      return interp_tier_dispatch_rewritten_return(cpu, _rpc24, 0xbdf8a1u);
    }
    if (_ret_s != _entry_s &&
        (uint16)(_entry_s - _ret_s) < 0x8000u &&
        !cpu_dispatch_has_entry(cpu, _rpc24)) {
      RecompStackPop();
      return interp_tier_dispatch_popped_return(cpu, _rpc24, 0xbdf8a1u,
          (uint16)(_entry_s + 2u));
    }
    cpu_trace_mark_nlr_exit(BD_EXIT_KIND_TRAMPOLINE);
    RecompStackPop();
    return cpu_dispatch_pc_from(cpu, _rpc24, (uint16)(_entry_s + 2u),
                                0xbdf8a1u);  /* RTS dispatch */ }
}
