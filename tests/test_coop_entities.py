from pathlib import Path
import importlib.util
import re
import unittest

import test_coop_mod as native_contract

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "coop_entity_adapter", ROOT / "scripts/apply_dkc1_coop_entities.py")
adapter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(adapter)

# Independent ENT1 byte fixtures: the two accepted v1 shapes include two
# trailing alignment bytes. Build bytes directly, never through the writer.
ENTITY_WIRE_HARNESS = r'''
#include "dkc1_coop_entities.c"
#include <assert.h>
int Dkc1CoopEnabled(void) { return 0; }
uint16_t Dkc1CoopCompanionBinding(void) { return 0; }
void Dkc1CoopRestoreCompanionBinding(uint16_t b) {}
void Dkc1CoopActorAllocated(uint16_t h) {}
void Dkc1CoopActorCreated(CpuState *c,uint16_t h) {}
void Dkc1CoopCrateHit(const uint8_t *r,uint16_t h) {}
void Dkc1CoopGraphicsSeedNative(void) {}
void Dkc1CoopGraphicsReset(void) {}
void Dkc1CoopGraphicsResetEntity(uint16_t h) {}
uint8 cpu_read8(CpuState *c,uint8 b,uint16 a) { return c->ram[a]; }
uint16 cpu_read16(CpuState *c,uint8 b,uint16 a) { return c->ram[a] | (c->ram[(uint16)(a+1)]<<8); }
void cpu_write8(CpuState *c,uint8 b,uint16 a,uint8 v) { c->ram[a]=v; }
void cpu_write16(CpuState *c,uint8 b,uint16 a,uint16 v) { c->ram[a]=(uint8)v; c->ram[(uint16)(a+1)]=(uint8)(v>>8); }
int main(void) {
  uint8_t current[13272], legacy[13240], expected[13272], actual[13272];
  /* ENT1 v1 row identities, independent of the current field declaration.
   * Round-trip bytes alone cannot catch a reordered address-to-row map. */
  static const uint16_t historical_fields[51] = {
    0x0a7d,0x0ab1,0x0ae5,0x0b19,0x0b8d,0x0bc1,0x0c35,0x0c69,
    0x0cdd,0x0d11,0x0d45,0x0db9,0x0ded,0x0e21,0x0e55,0x0e89,
    0x0ebd,0x0ef1,0x0f25,0x0f59,0x0f8d,0x0fc1,0x0ff5,0x1029,
    0x109d,0x10d1,0x1105,0x1139,0x116d,0x11a1,0x11d5,0x1209,
    0x123d,0x1271,0x12a5,0x12d9,0x130d,0x1341,0x1375,0x13e9,
    0x145d,0x1491,0x14c5,0x14f9,0x152d,0x1561,0x1595,0x15c9,
    0x15fd,0x1631,0x1665
  };
  uint8_t ram[65536] = {0};
  assert(Dkc1CoopEntitiesStateSize()==13272);
  for(unsigned i=0;i<13272;i++) current[i]=(uint8_t)(i*37+11);
  current[0]='E';current[1]='N';current[2]='T';current[3]='1';
  current[4]=1;current[5]=current[6]=current[7]=0;
  /* Deliberately noncanonical active/owner bytes are accepted verbatim. */
  Dkc1CoopEntitiesReset();
  assert(Dkc1CoopEntitiesLoadState(current,13272));
  assert(Dkc1CoopEntitiesSaveState(actual,13272));
  assert(!memcmp(actual,current,13272)); /* Includes both tail bytes. */
  assert(Dkc1CoopEffectView(0x34)==current[80]);
  assert(Dkc1CoopEffectView(0x72)==current[111]);
  for(unsigned field=0;field<51;field++)
    for(int delta=-1;delta<=1;delta++)
      for(unsigned h=0x80;h<=0xff;h++) {
        unsigned at=112+field*258+h+delta;
        uint16_t value=current[at]|((uint16_t)current[at+1]<<8);
        assert(Dkc1CoopEntityRead16(ram,historical_fields[field]+delta,h)==value);
      }
  for(unsigned base=0;base<=0xffff;base++) {
    int found=0;
    for(unsigned field=0;field<51;field++)
      found|=base>=historical_fields[field]-1u && base<=historical_fields[field]+1u;
    assert(Dkc1CoopEntityIsField((uint16_t)base)==found);
  }
  memcpy(legacy,current,80);
  for(unsigned i=0;i<13158;i++) legacy[80+i]=(uint8_t)(i*19+7);
  legacy[13238]=0xa6;legacy[13239]=0x59;
  memset(expected,0,13272);memcpy(expected,legacy,80);
  memcpy(expected+112,legacy+80,13158);
  assert(Dkc1CoopEntitiesLoadState(legacy,13240));
  assert(Dkc1CoopEntitiesSaveState(actual,13272));
  assert(!memcmp(actual,expected,13272));
  assert(Dkc1CoopEffectView(0x34)==0 && Dkc1CoopEffectView(0x72)==0);
  for(unsigned field=0;field<51;field++) {
    unsigned at=80+field*258+0x80;
    assert(Dkc1CoopEntityRead16(ram,historical_fields[field],0x80)==
        (uint16_t)(legacy[at]|((uint16_t)legacy[at+1]<<8)));
  }
  /* Historical padding belongs to the length even though migration drops it. */
  assert(!Dkc1CoopEntitiesLoadState(legacy,13238));
  assert(!Dkc1CoopEntitiesLoadState(current,13270));
  assert(!Dkc1CoopEntitiesSaveState(legacy,13240));
  return 0;
}
'''


class CoopEntityTests(unittest.TestCase):
    def test_historical_ent1_bytes_and_padded_lengths(self):
        native_contract.CoopModTests.compile_harness(self, ENTITY_WIRE_HARNESS)

    def test_host_actor_accessors_match_authoritative_generated_bases(self):
        native = (ROOT / "runner/dkc1_wram_gen.h").read_text()
        wrapper = (ROOT / "runner/dkc1_coop_actor.h").read_text()
        bases = {name: int(base, 16) for name, base in re.findall(
            r"static inline uint16_t Dkc1Actor_(\w+)\([^\n]+\) \{\s+"
            r"return Dkc1WramU16\(wram, 0x([0-9A-F]+)u \+ slot\);", native)
            if int(base, 16) in adapter.TABLES}
        got = {name: int(base, 16) for name, base in re.findall(
            r"#define Dkc1Actor_(\w+)\(wram, slot\) \\\n"
            r"  Dkc1CoopEntityRead16\(\(wram\), 0x([0-9a-f]+)u,", wrapper)}
        self.assertEqual(len(bases), 51)
        self.assertEqual(got, bases)
        for name, base in bases.items():
            self.assertIn(f"#define Dkc1Actor_Set{name}(wram, slot, value)", wrapper)
            self.assertIn(f"Dkc1CoopEntityWrite16((wram), 0x{base:04x}u,", wrapper)

    def test_storage_pool_bookmarks_transactions_and_toggle(self):
        native_contract.CoopModTests.compile_harness(self, r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_state.h"
#include "dkc1_coop_entities.h"
static int enabled, seeds, resets, allocations;
static unsigned bus_calls;
int Dkc1CoopEnabled(void) { return enabled; }
static uint16_t companion;
uint16_t Dkc1CoopCompanionBinding(void){return companion;}
void Dkc1CoopRestoreCompanionBinding(uint16_t b){companion=b;}
void Dkc1CoopActorAllocated(uint16_t h) {}
void Dkc1CoopActorCreated(CpuState *c,uint16_t h) {}
void Dkc1CoopCrateHit(const uint8_t *r,uint16_t h) {}
void Dkc1CoopGraphicsSeedNative(void) { ++seeds; assert(Dkc1CoopEntitiesActive()); }
void Dkc1CoopGraphicsReset(void) { ++resets; }
void Dkc1CoopGraphicsResetEntity(uint16_t h) { assert(Dkc1CoopEntityIsHandle(h)); ++allocations; }
uint8 cpu_read8(CpuState *c, uint8 b, uint16 a) { ++bus_calls; return c->open_bus=c->ram[a]; }
uint16 cpu_read16(CpuState *c, uint8 b, uint16 a) {
  uint16 v=cpu_read8(c,b,a); return v | ((uint16)cpu_read8(c,b,(uint16)(a+1))<<8);
}
void cpu_write8(CpuState *c, uint8 b, uint16 a, uint8 v) { ++bus_calls; c->ram[a]=v; c->open_bus=v; }
void cpu_write16(CpuState *c, uint8 b, uint16 a, uint16 v) {
  cpu_write8(c,b,a,(uint8)v); cpu_write8(c,b,(uint16)(a+1),(uint8)(v>>8));
}
int main(void) {
  uint8_t ram[0x20000], saved_ram[0x20000]; CpuState c;
  memset(ram,0xa5,sizeof ram); memset(&c,0,sizeof c); c.ram=ram;
  Dkc1CoopEntitiesReset();
  assert(!Dkc1CoopEntitiesActive()); assert(!Dkc1CoopEntityIsHandle(0x80));
  assert(Dkc1CoopEntityNextHandle(0x32)==0x34);
  c.X=6; assert(!Dkc1CoopEntityAllocate(&c,0x1e)); assert(c.X==6);
  Dkc1CoopIndexedWrite16(&c,0,0x0b19,0x80,0x1234);
  assert(ram[0xb99]==0x34 && ram[0xb9a]==0x12 && bus_calls==2);
  assert(seeds==0 && allocations==0);
  assert(Dkc1CoopEntityLiveLoopIndex(0x34)==0x34);
  assert(Dkc1CoopEntityPoolBackwardIndex(0x7e,0x1e)==0x7e);
  unsigned prior_bus_calls=bus_calls;
  Dkc1CoopEntityCollisionCursor(&c);
  assert(bus_calls==prior_bus_calls); /* Disabled adapter is bus-inert. */
  enabled=1; assert(Dkc1CoopEntitiesActive()); assert(seeds==1);
  assert(Dkc1CoopEntityLiveLoopIndex(2)==2);
  assert(Dkc1CoopEntityLiveLoopIndex(0x32)==0x32);
  assert(Dkc1CoopEntityLiveLoopIndex(0x34)==0x100);
  Dkc1CoopEntityWrite16(ram,0x0d45,0x82,0xff00); /* Full ID word is the guard. */
  Dkc1CoopEntityWrite16(ram,0x0d45,0xfe,0x15);
  assert(Dkc1CoopEntityLiveLoopIndex(0x34)==0x82);
  assert(Dkc1CoopEntityLiveLoopIndex(0x84)==0xfe);
  assert(Dkc1CoopEntityLiveLoopIndex(0x100)==0x100);
  Dkc1CoopEntityWrite16(ram,0x0d45,0x80,0x16); /* Spawn since the last query. */
  assert(Dkc1CoopEntityLiveLoopIndex(0x34)==0x80);
  Dkc1CoopEntityClearRange(0x0d45,0x34);
  assert(Dkc1CoopEntityLiveLoopIndex(0x34)==0x100);
  assert(Dkc1CoopEntityLoopIndex(0x34)==0x80); /* Exhaustive clear/hide loops. */
  memcpy(saved_ram,ram,sizeof ram); bus_calls=0;
  for (uint16_t h=0x80; h<=0xfe; h+=2) {
    assert(Dkc1CoopEntityIsHandle(h)); assert(h!=0xff);
    assert(Dkc1CoopEntityAllocate(&c,h&2?0x34:0x1e)); assert(c.X==h);
    Dkc1CoopEntityAllocated(&c);
    Dkc1CoopIndexedWrite16(&c,0xb5,0x0d45,h,0x200+h);
    Dkc1CoopIndexedWrite16(&c,0x7e,0x0b19,h,0x8000+h);
    Dkc1CoopIndexedWrite16(&c,0x80,0x0b8d,h,0x4000+h);
    Dkc1CoopIndexedWrite16(&c,0,0x15fd,h,0x9000+h);
    assert(Dkc1CoopEntityRead16(ram,0x0b19,h)==0x8000+h);
    assert(Dkc1CoopEntityRead16(ram,0x0b8d,h)==0x4000+h);
    assert(Dkc1CoopEntityRead16(ram,0x15fd,h)==0x9000+h);
  }
  assert(!memcmp(saved_ram,ram,sizeof ram)); assert(!bus_calls);
  assert(allocations==64 && seeds==1);
  assert(!Dkc1CoopEntityAllocate(&c,0x1e));
  assert(!Dkc1CoopEntityIsHandle(0xff));
  assert(!Dkc1CoopEntityIsHandle(0x34));
  assert(Dkc1CoopEntityNextHandle(0x32)==0x80);
  assert(Dkc1CoopEntityNextHandle(0xfe)==0x100);
  assert(Dkc1CoopEntityPreviousHandle(0x80)==0x32);
  assert(Dkc1CoopEntityPoolLoopIndex(0x1e,0x1e)==0x80);
  assert(Dkc1CoopEntityPoolLoopIndex(0x34,0x34)==0x82);
  assert(Dkc1CoopEntityBackwardIndex(0x7e)==0x32);
  assert(Dkc1CoopEntityPoolBackwardIndex(0xfe,0x1e)==0xfc);
  assert(Dkc1CoopEntityPoolBackwardIndex(0x82,0x1e)==0x80);
  assert(Dkc1CoopEntityPoolBackwardIndex(0x7e,0x1e)==0x1c);
  assert(Dkc1CoopEntityPoolBackwardIndex(4,0x1e)==4);
  Dkc1CoopIndexedWrite16(&c,0,0x0c69,0xfe,0x1234);
  Dkc1CoopIndexedWrite8(&c,0,0x0c6a,0xfe,0xab);
  assert(Dkc1CoopIndexedRead16(&c,0,0x0c69,0xfe)==0xab34);
  assert(c.open_bus==0xab);
  Dkc1CoopIndexedWrite16(&c,0,0x1208,0x82,0x9876);
  assert(Dkc1CoopIndexedRead8(&c,0,0x1209,0x82)==0x98);
  /* Dynamic actor-script destination before/after pointer addition. */
  c.D=0; ram[0x50]=0x80; ram[0x51]=0; c.X=0x0b19+0x80;
  Dkc1CoopEntityScriptWrite16(&c,0xface);
  assert(Dkc1CoopEntityRead16(ram,0x0b19,0x80)==0xface);
  /* Inactive effects still access their authentic WRAM field. */
  Dkc1CoopIndexedWrite16(&c,0,0x0d45,0x34,0x4567);
  assert(ram[0x0d79]==0x67 && ram[0x0d7a]==0x45);
  ram[0x056f]=2;
  Dkc1CoopIndexedWrite8(&c,0,0x0d45,0x34,0x42);
  assert(Dkc1CoopEffectView(0x34)==1);
  ram[0x056f]=1;
  Dkc1CoopEntityWrite16(ram,0x0d45,0x36,0x42);
  assert(Dkc1CoopEffectView(0x36)==0);
  ram[0x056f]=2;
  Dkc1CoopIndexedWrite16(&c,0,0x0d45,0x38,0x41);
  assert(Dkc1CoopEffectView(0x38)==1);
  ram[0x056f]=1;
  Dkc1CoopEntityWrite16(ram,0x0d45,0x3a,0x41);
  assert(Dkc1CoopEffectView(0x3a)==0);
  Dkc1CoopEntityWrite16(ram,0x0d45,0x38,0);
  assert(Dkc1CoopEffectView(0x38)==0);
  companion=0x108;
  assert(Dkc1CoopEntitiesBeginTransaction());
  Dkc1CoopEntityWrite16(ram,0x0d45,0x34,0);
  companion=0;
  assert(Dkc1CoopEffectView(0x34)==0);
  Dkc1CoopEntitiesEndTransaction(0);
  assert(companion==0x108);
  assert(Dkc1CoopEffectView(0x34)==1);
  assert(Dkc1CoopEntitiesBeginTransaction());
  assert(!Dkc1CoopEntitiesBeginTransaction());
  Dkc1CoopEntityWrite16(ram,0x0d45,0x80,0);
  Dkc1CoopEntitiesEndTransaction(0);
  assert(Dkc1CoopEntityRead16(ram,0x0d45,0x80)==0x280);
  assert(Dkc1CoopEntitiesBeginTransaction());
  Dkc1CoopEntityWrite16(ram,0x0d45,0x80,0);
  Dkc1CoopEntitiesEndTransaction(1);
  assert(Dkc1CoopEntityAllocate(&c,0x34) && c.X==0x80);
  /* Temporary native ID hiding must retain graphics metadata. */
  assert(allocations==64);
  size_t size=Dkc1CoopEntitiesStateSize(); void *snapshot=malloc(size);
  assert(snapshot && Dkc1CoopEntitiesSaveState(snapshot,size));
  enabled=0; assert(Dkc1CoopEntitiesActive());
  Dkc1CoopEntitiesPoolReset(); assert(!Dkc1CoopEntitiesActive());
  assert(resets==1);
  assert(Dkc1CoopEntitiesLoadState(snapshot,size));
  assert(Dkc1CoopEffectView(0x34)==1);
  assert(Dkc1CoopEntitiesActive()); assert(seeds==1);
  uint8_t *legacy=malloc(size-32);
  assert(legacy);
  memcpy(legacy,snapshot,80);
  memcpy(legacy+80,(uint8_t*)snapshot+112,size-112);
  assert(Dkc1CoopEntitiesLoadState(legacy,size-32));
  assert(Dkc1CoopEffectView(0x34)==0);
  assert(Dkc1CoopEntityRead16(ram,0x0b19,0xfe)==0x80fe);
  assert(Dkc1CoopEntityRead16(ram,0x0d45,0xfe)==0x2fe);
  assert(!Dkc1CoopEntitiesSaveState(legacy,size-32));
  free(legacy);
  assert(Dkc1CoopEntitiesLoadState(snapshot,size));
  assert(!Dkc1CoopEntitiesLoadState(snapshot,size-1));
  ((uint8_t*)snapshot)[0]^=1;
  assert(!Dkc1CoopEntitiesLoadState(snapshot,size));
  Dkc1CoopEntityClearRange(0x0b19,0x34);
  assert(Dkc1CoopEntityRead16(ram,0x0b19,0xfe)==0);
  assert(Dkc1CoopEntityRead16(ram,0x0b8d,0xfe)==0x40fe);
  free(snapshot);
  return 0;
}
''', ["runner/dkc1_coop_entities.c"])

    def test_adapter_preserves_literal_fields_and_unrelated_bus_access(self):
        text = '''cpu_read16(cpu, (uint8)((((uint32)cpu->DB << 16) + (uint32)0x0b19 + (uint32)cpu->X) >> 16), (uint16)(((uint32)cpu->DB << 16) + (uint32)0x0b19 + (uint32)cpu->X));
cpu_write8(cpu, 0, (uint16)(cpu->D + 0x0000 + cpu->X), value);
cpu_read16(cpu, 0, 0x0b99);'''
        result, count = adapter.adapt_indexed_accesses(text)
        self.assertEqual(count, 1)
        self.assertIn("Dkc1CoopIndexedRead16(cpu, cpu->DB, 0x0b19u, cpu->X)", result)
        self.assertIn("cpu_read16(cpu, 0, 0x0b99)", result)
        self.assertIn("cpu_write8(cpu, 0,", result)
        self.assertEqual(adapter.adapt_indexed_accesses(result), (result, 0))

    def test_indirect_initializer_and_script_adapters(self):
        original = "cpu_write16(cpu, 0x00, (uint16)(cpu->D + 0x0000 + cpu->X), _v2);"
        init = adapter._indirect_write(original)
        self.assertEqual(init, "Dkc1CoopIndexedWrite16(cpu, 0x00, cpu->D, cpu->X, _v2);")
        self.assertEqual(adapter._indirect_write(init), init)
        self.assertEqual(adapter._indirect_write(original, True),
                         "Dkc1CoopEntityScriptWrite16(cpu, _v2);")

    def test_nested_pointer_load_rewrites_only_the_inner_access(self):
        inner = "cpu_read16(cpu, cpu->DB, (uint16)(((uint32)cpu->DB << 16) + (uint32)0x0b19 + (uint32)cpu->X))"
        original = f"cpu_read16(cpu, 0, {inner})"
        result, count = adapter.adapt_indexed_accesses(original)
        self.assertEqual(count, 1)
        self.assertEqual(result,
            "cpu_read16(cpu, 0, Dkc1CoopIndexedRead16(cpu, cpu->DB, 0x0b19u, cpu->X))")

    def test_current_generated_graph_is_adaptable_and_idempotent(self):
        paths = list((ROOT / "generated/snesrecomp").glob("*.c"))
        if not paths:
            self.skipTest("private generated game code unavailable")
        sources = {p: p.read_text(encoding="utf-8") for p in paths}
        adapter.apply_overrides(sources)
        first = dict(sources)
        adapter.apply_overrides(sources)
        self.assertEqual(sources, first)
        joined = "\n".join(sources.values())
        self.assertIn("Dkc1CoopEntityScriptWrite16(cpu,", joined)
        self.assertIn("Dkc1CoopEntityAllocated(cpu)", joined)
        self.assertEqual(joined.count("if (Dkc1CoopIndependentDamage(cpu))"), 2)
        self.assertNotIn("Dkc1CoopIndependentWaterSink(cpu)", joined)
        water = next(t for t in sources.values() if "RecompReturn CODE_BFA3AE_M0X0(CpuState *cpu) {" in t)
        body = water.split("  L_A3F8_M0X0:", 1)[1]
        self.assertLess(body.index("CPU_STACK_OP_PLX"), body.index("if (Dkc1CoopIndependentDamage(cpu))"))
        self.assertLess(body.index("if (Dkc1CoopIndependentDamage(cpu))"), body.index("uint8 _v = (uint8)(cpu->X & 0xFF)"))
        self.assertEqual(joined.count("Dkc1CoopEntityLiveLoopIndex(cpu->X)"), 5)
        self.assertEqual(joined.count("Dkc1CoopEntityCollisionCursor(cpu)"), 2)
        self.assertNotIn(("CODE_BBA849_M0X0", "AA2C", "X", 0x34), adapter.FORWARD_LOOPS)

    def test_empty_skip_is_restricted_to_five_id_guarded_loops(self):
        self.assertEqual(adapter.LIVE_FORWARD_LOOPS, {
            ("CODE_BF8000_M0X0", "8035"), ("CODE_BF804B_M0X0", "806F"),
            ("CODE_BF8091_M0X0", "80B7"), ("CODE_BF8091_M0X0", "80E6"),
            ("CODE_BF80F0_M0X0", "8119"),
        })
        for function, label, register, bound in adapter.FORWARD_LOOPS:
            fixture = (f"  L_{label}_M0X0:\n    uint16 _v1 = 0x{bound:x};\n"
                       f"    uint16 _v2 = cpu_read_{register.lower()}16(cpu);\n")
            old = adapter._adapt_forward(fixture, label, register, bound)
            live = (function, label) in adapter.LIVE_FORWARD_LOOPS
            result = adapter._adapt_forward(old, label, register, bound, live)
            self.assertEqual("Dkc1CoopEntityLiveLoopIndex" in result, live)
            self.assertEqual(adapter._adapt_forward(result, label, register, bound, live), result)
