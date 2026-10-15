#ifndef DKC1_COOP_ACTOR_H
#define DKC1_COOP_ACTOR_H

/* Scoped actor views: cartridge fields stay authoritative for native handles.
 * Only co-op clients include this wrapper; diagnostics retain raw WRAM views.
 * Addresses are checked against dkc1_wram_gen.h by test_coop_entities.py. */
#include "dkc1_wram_gen.h"
#include "dkc1_coop_entities.h"

#define Dkc1Actor_CurrentOAMZPosLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0a7du, (uint16_t)(slot))
#define Dkc1Actor_SetCurrentOAMZPosLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0a7du, (uint16_t)(slot), (value))

#define Dkc1Actor_DrawOrderIndexLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0ab1u, (uint16_t)(slot))
#define Dkc1Actor_SetDrawOrderIndexLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0ab1u, (uint16_t)(slot), (value))

#define Dkc1Actor_DisplayedPoseLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0ae5u, (uint16_t)(slot))
#define Dkc1Actor_SetDisplayedPoseLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0ae5u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprXPos(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0b19u, (uint16_t)(slot))
#define Dkc1Actor_SetSprXPos(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0b19u, (uint16_t)(slot), (value))

#define Dkc1Actor_OAMZPosLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0b8du, (uint16_t)(slot))
#define Dkc1Actor_SetOAMZPosLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0b8du, (uint16_t)(slot), (value))

#define Dkc1Actor_SprYPos(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0bc1u, (uint16_t)(slot))
#define Dkc1Actor_SetSprYPos(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0bc1u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprSurfaceY(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0c35u, (uint16_t)(slot))
#define Dkc1Actor_SetSprSurfaceY(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0c35u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprFlags(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0c69u, (uint16_t)(slot))
#define Dkc1Actor_SetSprFlags(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0c69u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0CDDLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0cddu, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0CDDLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0cddu, (uint16_t)(slot), (value))

#define Dkc1Actor_SprAnimFrame(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0d11u, (uint16_t)(slot))
#define Dkc1Actor_SetSprAnimFrame(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0d11u, (uint16_t)(slot), (value))

#define Dkc1Actor_SpriteIDLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0d45u, (uint16_t)(slot))
#define Dkc1Actor_SetSpriteIDLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0d45u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprXSubpixel(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0db9u, (uint16_t)(slot))
#define Dkc1Actor_SetSprXSubpixel(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0db9u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0DEDLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0dedu, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0DEDLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0dedu, (uint16_t)(slot), (value))

#define Dkc1Actor_SprYSubpixel(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0e21u, (uint16_t)(slot))
#define Dkc1Actor_SetSprYSubpixel(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0e21u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0E55Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0e55u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0E55Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0e55u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprXVelocity(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0e89u, (uint16_t)(slot))
#define Dkc1Actor_SetSprXVelocity(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0e89u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0EBDLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0ebdu, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0EBDLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0ebdu, (uint16_t)(slot), (value))

#define Dkc1Actor_SprYVelocity(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0ef1u, (uint16_t)(slot))
#define Dkc1Actor_SetSprYVelocity(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0ef1u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0F25Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0f25u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0F25Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0f25u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0F59Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0f59u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0F59Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0f59u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0F8DLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0f8du, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0F8DLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0f8du, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0FC1Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0fc1u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0FC1Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0fc1u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable0FF5Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x0ff5u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable0FF5Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x0ff5u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable1029Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1029u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable1029Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1029u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable109DLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x109du, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable109DLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x109du, (uint16_t)(slot), (value))

#define Dkc1Actor_SprAnimID(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x10d1u, (uint16_t)(slot))
#define Dkc1Actor_SetSprAnimID(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x10d1u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprAnimTimer(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1105u, (uint16_t)(slot))
#define Dkc1Actor_SetSprAnimTimer(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1105u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprAnimSpeed(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1139u, (uint16_t)(slot))
#define Dkc1Actor_SetSprAnimSpeed(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1139u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprAnimScriptPtr(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x116du, (uint16_t)(slot))
#define Dkc1Actor_SetSprAnimScriptPtr(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x116du, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable11A1Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x11a1u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable11A1Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x11a1u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable11D5Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x11d5u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable11D5Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x11d5u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprSurfaceAttr(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1209u, (uint16_t)(slot))
#define Dkc1Actor_SetSprSurfaceAttr(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1209u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable123DLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x123du, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable123DLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x123du, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable1271Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1271u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable1271Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1271u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprCollisionFlags(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x12a5u, (uint16_t)(slot))
#define Dkc1Actor_SetSprCollisionFlags(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x12a5u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable12D9Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x12d9u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable12D9Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x12d9u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprAnimCallbackBank(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x130du, (uint16_t)(slot))
#define Dkc1Actor_SetSprAnimCallbackBank(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x130du, (uint16_t)(slot), (value))

#define Dkc1Actor_SprAnimCallbackAddr(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1341u, (uint16_t)(slot))
#define Dkc1Actor_SetSprAnimCallbackAddr(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1341u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable1375Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1375u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable1375Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1375u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable13E9Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x13e9u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable13E9Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x13e9u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable145DLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x145du, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable145DLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x145du, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable1491Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1491u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable1491Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1491u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable14C5Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x14c5u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable14C5Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x14c5u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable14F9Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x14f9u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable14F9Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x14f9u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable152DLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x152du, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable152DLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x152du, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable1561Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1561u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable1561Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1561u, (uint16_t)(slot), (value))

#define Dkc1Actor_SprEventFlags(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1595u, (uint16_t)(slot))
#define Dkc1Actor_SetSprEventFlags(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1595u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable15C9Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x15c9u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable15C9Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x15c9u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable15FDLo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x15fdu, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable15FDLo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x15fdu, (uint16_t)(slot), (value))

#define Dkc1Actor_SprSurfaceDistance(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1631u, (uint16_t)(slot))
#define Dkc1Actor_SetSprSurfaceDistance(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1631u, (uint16_t)(slot), (value))

#define Dkc1Actor_RAMTable1665Lo(wram, slot) \
  Dkc1CoopEntityRead16((wram), 0x1665u, (uint16_t)(slot))
#define Dkc1Actor_SetRAMTable1665Lo(wram, slot, value) \
  Dkc1CoopEntityWrite16((wram), 0x1665u, (uint16_t)(slot), (value))

#endif
