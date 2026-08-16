#ifndef DKC1_INVARIANT_MONITOR_H
#define DKC1_INVARIANT_MONITOR_H

/* Live widescreen invariant monitor.
 *
 * Every frame, cross-checks the chain an authored object travels —
 * scanner window -> actor pool -> $192B bookkeeping -> WRAM OAM shadow ->
 * PPU OAM — plus the BG margin integrity counters, and classifies the
 * FIRST bad frame with a subsystem verdict instead of leaving "something
 * disappeared" to human eyes.
 *
 * Env-gated: DKC1_INVARIANT_MONITOR=<jsonl path> (or "1" for counters
 * only). Verdicts use transition discipline (an episode must establish
 * itself before its absence can fail) so steady-state noise cannot flood.
 * Host-side only; never touches emulated state.
 */
void Dkc1InvariantMonitorFrame(long host_frame);

/* Cumulative verdict count (auto-export trigger). */
long Dkc1InvariantMonitorTotal(void);

/* One-line class summary for the debug panel, e.g.
 * "no-oam:1 oam-lag:0 xhigh:0 sim-out:0 raw:0 stale:2". Returns `buffer`. */
const char *Dkc1InvariantMonitorSummary(char *buffer, unsigned size);

#endif
