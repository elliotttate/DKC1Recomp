from pathlib import Path
import importlib.util
import sys
import unittest

import test_coop_mod as native_contract

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import apply_dkc1_coop_schedule as adapter


class CoopScheduleTests(unittest.TestCase):
    def test_exact_four_aot_calls_and_idempotent_cleanup(self):
        sources = {}
        for owner in dict.fromkeys(row[0] for row in adapter.CALLS):
            blocks = []
            for _, call, target, ret, overlay in (r for r in adapter.CALLS if r[0] == owner):
                blocks.append(f'''  L_{call & 0xffff:04X}_M0X0:
    cpu_write16(cpu, 0x00, cpu->D + 0x82, cpu->X);
''' + ('    Dkc1VideoBeginPlacedActorDispatch(cpu);\n' if overlay else '') + f'''    {{
      /* JSL return frame -> cpu->S (Option-1) */
      cpu_write8(cpu, 0, cpu->S--, 0xbf);
      cpu_trace_pb_change(cpu, 0x{call:06x}u, _saved_pb, 0xbf, CPU_TR_JSL);
      CODE_{target:06X}_M0X0(cpu);
    }}
''' + ('    Dkc1VideoEndPlacedActorDispatch(cpu);\n' if overlay else '') + f'''    goto L_{ret & 0xffff:04X}_M0X0;
  L_{ret & 0xffff:04X}_M0X0:
    cpu->X += 2;
''')
            sources[Path(owner + '.c')] = f'RecompReturn {owner}(CpuState *cpu) {{\n' + ''.join(blocks) + '}\n'
        self.assertEqual(adapter.apply_overrides(sources), {'coop_schedule': 4})
        before = dict(sources)
        self.assertEqual(adapter.apply_overrides(sources), {'coop_schedule': 0})
        self.assertEqual(before, sources)
        for owner, call, target, ret, overlay in adapter.CALLS:
            body = sources[Path(owner + '.c')].split(f'L_{call & 0xffff:04X}_M0X0:', 1)[1].split(f'\n  L_{ret & 0xffff:04X}_M0X0:', 1)[0]
            self.assertLess(body.index('Dkc1CoopScheduleActor'), body.index('/* JSL return frame'))
            if overlay:
                self.assertLess(body.index('Dkc1VideoBegin'), body.index('Dkc1CoopScheduleActor'))
                self.assertEqual(body.count('Dkc1VideoEndPlacedActorDispatch'), 2)
        # Run the earlier phase-guard pass after scheduling: it must recognize
        # the two mutually exclusive End paths on a second full adaptation.
        import apply_dkc1_widescreen_overrides as wide
        path = next(p for p in sources if 'BF8000' in str(p))
        sources[path] = sources[path].replace('L_802F_M0X0:', 'L_802D_M0X0:')
        path = next(p for p in sources if 'BF804B' in str(p))
        sources[path] = sources[path].replace('L_8069_M0X0:', 'L_8067_M0X0:')
        sources[Path('dispatch.c')] = 'RecompReturn CODE_BF8087_M0X0(CpuState *cpu) {\n}\n'
        wide.adapt_placed_actor_phase_guard(sources)

    def test_participant_failure_unwinds_reverse_once_and_preserves_commit(self):
        native_contract.CoopModTests.compile_harness(self, r'''
#include <assert.h>
#include "dkc1_coop_transaction.h"
static int events[20],n,fail;
static int a(void){events[n++]=1;return fail!=1;}
static int b(void){events[n++]=2;return fail!=2;}
static int c(void){events[n++]=3;return fail!=3;}
static void ea(int commit){events[n++]=10+commit;}
static void eb(int commit){events[n++]=20+commit;}
static void ec(int commit){events[n++]=30+commit;}
int main(void){
 const Dkc1CoopTransactionParticipant p[]={{a,ea},{b,eb},{c,ec}};
 for(fail=1;fail<=3;fail++){
  n=0;Dkc1CoopTransaction t={0};
  assert(!Dkc1CoopTransactionBegin(&t,p,3));
  assert(n==fail*2-1 && !t.opened);
  for(int i=0;i<fail;i++)assert(events[i]==i+1);
  for(int i=1;i<fail;i++)assert(events[fail+i-1]==10*(fail-i));
  Dkc1CoopTransactionEnd(&t,1);assert(n==fail*2-1);
 }
 fail=0;n=0;Dkc1CoopTransaction t={0};
 assert(Dkc1CoopTransactionBegin(&t,p,3));
 Dkc1CoopTransactionEnd(&t,1);
 assert(n==6 && events[3]==31 && events[4]==21 && events[5]==11);
 Dkc1CoopTransactionEnd(&t,0);assert(n==6);
 return 0;
}
''')

    def test_isolated_core_parity_and_complete_failed_attempt_rollback(self):
        if not (ROOT / 'generated/host/dkc1_coop_interp_rules.inc').exists():
            self.skipTest('private interpreter rules unavailable')
        native_contract.CoopModTests.compile_harness(self,
            (ROOT / 'tests/coop_schedule_harness.c').read_text(),
            ['runner/dkc1_coop_schedule.c', 'runner/dkc1_coop_entities.c',
             'runner/dkc1_coop_graphics.c', 'runner/dkc1_coop_movement.c',
             'snesrecomp/runner/src/snes/interp816.c'])
