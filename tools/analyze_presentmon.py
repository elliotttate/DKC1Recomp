#!/usr/bin/env python3
"""Correlate PresentMon v1 API timestamps to each logged DKC1 submission.

API-only ETW validates submission coverage and order, NOT scanout. Display
acceptance separately requires DXGI frame statistics. Missing ETW data fails
closed; a running collector or a filename alone is not evidence.
"""
import bisect
import csv
from pathlib import Path


def analyze(path, pid, frames):
    path = Path(path)
    result = {'passed': False, 'source': 'PresentMon ETW API',
              'display_verified': False, 'path': str(path)}
    if not path.is_file():
        return {**result, 'error': 'PresentMon produced no CSV'}
    if not frames or not all('submit_qpc_ms' in r and 'present_end_qpc_ms' in r for r in frames):
        return {**result, 'error': 'Host absolute QPC timestamps are unavailable'}
    with path.open(newline='', encoding='utf-8-sig') as stream:
        reader = csv.DictReader(stream)
        required = {'ProcessID', 'Runtime', 'QPCTime', 'SwapChainAddress'}
        if not required.issubset(reader.fieldnames or []):
            return {**result, 'error': 'Expected PresentMon v1 QPC schema'}
        records = [row for row in reader if int(row['ProcessID']) == pid and row['Runtime'] == 'DXGI']
    if not records:
        return {**result, 'error': 'No ETW submissions for the recorded game PID'}
    # PresentMon 2.3 v1 writes QPCTime in seconds even with --qpc_time_ms;
    # accept seconds or milliseconds only when absolute host QPC proves it.
    raw = [float(r['QPCTime']) for r in records]
    lo, hi = frames[0]['submit_qpc_ms'], frames[-1]['present_end_qpc_ms']
    scales = [s for s in (1.0, 1000.0) if any(lo-.1 <= q*s <= hi+.1 for q in raw)]
    if len(scales) != 1:
        return {**result, 'error': 'Cannot align ETW and host QPC clocks'}
    scale = scales[0]
    selected = [(q*scale, r) for q,r in zip(raw, records) if lo-.1 <= q*scale <= hi+.1]
    times = [q for q,r in selected]
    if times != sorted(times):
        return {**result, 'error': 'ETW submission timestamps are out of order'}
    counts, offsets = [], []
    for frame in frames:
        start = bisect.bisect_left(times, frame['submit_qpc_ms']-.1)
        stop = bisect.bisect_right(times, frame['present_end_qpc_ms']+.1)
        counts.append(stop-start)
        if stop-start == 1:
            offsets.append(times[start]-frame['submit_qpc_ms'])
    missing, multiple = sum(n==0 for n in counts), sum(n>1 for n in counts)
    result.update(expected_frames=len(frames), etw_submissions=len(times),
                  missing_frames=missing, multiple_submissions=multiple,
                  qpc_to_ms=scale, swapchains=len({r['SwapChainAddress'] for q,r in selected}),
                  max_submit_offset_ms=max(offsets, default=None))
    result['passed'] = (not missing and not multiple and len(times)==len(frames)
                        and result['swapchains']==1)
    return result
