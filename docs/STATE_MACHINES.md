# State-machine catalog (generated)

Regenerate static evidence: `python tools/state_catalog.py`. Add runtime evidence with `--lifecycle trace.jsonl`. Targets are listed in literal dispatch-table ordinal order; `(static)` facts are conservative textual references/immediate stores and do not cover computed or interprocedural values. `(observed)` is emitted only for matching native actor lifecycle rows and object-specific `NorSprXX` machines.

## DKC1_NorSpr29_Slippa_Main  (dispatch `0xBFC79A`, 3 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr29_Slippa_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr29_Slippa_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr29_Slippa_State2 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr06_Klump_Main  (dispatch `0xBFC818`, 3 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr06_Klump_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr06_Klump_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr06_Klump_State2 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr09_Rambi_Main  (dispatch `0xBFCAC7`, 8 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr09_Rambi_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr09_Rambi_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr09_Rambi_State2 [table-derived] | - | - | - | - | - | - |
| $03: NorSpr09_Rambi_State3 [table-derived] | - | - | - | - | - | - |
| $04: NorSpr09_Rambi_State4 [table-derived] | - | - | - | - | - | - |
| $05: NorSpr09_Rambi_State5 [table-derived] | - | - | - | - | - | - |
| $06: NorSpr09_Rambi_State6 [table-derived] | - | - | - | - | - | - |
| $07: NorSpr09_Rambi_State7 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr22_SteelKeg_Main  (dispatch `0xBFCE9B`, 10 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr22_SteelKeg_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr22_SteelKeg_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr22_SteelKeg_State2 [table-derived] | - | - | - | - | - | - |
| $03: NorSpr22_SteelKeg_State3 [table-derived] | - | - | - | $40 | $03 | - |
| $04: NorSpr22_SteelKeg_State4 [table-derived] | - | - | - | - | $02 | - |
| $05: NorSpr22_SteelKeg_State5 [table-derived] | - | - | - | - | - | - |
| $06: NorSpr22_SteelKeg_State6 [table-derived] | - | - | - | - | - | - |
| $07: NorSpr22_SteelKeg_State7 [table-derived] | - | - | - | - | - | - |
| $08: NorSpr22_SteelKeg_State8 [table-derived] | - | - | - | - | - | - |
| $09: NorSpr22_SteelKeg_State9 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr18_AnimalBuddyBox_Main  (dispatch `0xBFD370`, 4 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr18_AnimalBuddyBox_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr18_AnimalBuddyBox_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr18_AnimalBuddyBox_State2 [table-derived] | - | - | - | - | - | - |
| $03: NorSpr18_AnimalBuddyBox_State3 [table-derived] | - | AnimalBuddyBox_Open | OpenBox | - | $01 | - |

## DKC1_NorSpr41_UnknownSprite_Main  (dispatch `0xBFD49A`, 9 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr41_UnknownSprite_State0 [table-derived] | - | - | - | - | $05 | - |
| $01: NorSpr41_UnknownSprite_State1 [table-derived] | - | - | - | - | $03, $06 | - |
| $02: NorSpr41_UnknownSprite_State2 [table-derived] | - | - | - | - | $04 | - |
| $03: NorSpr41_UnknownSprite_State3 [table-derived] | - | - | - | - | - | - |
| $04: NorSpr41_UnknownSprite_State4 [table-derived] | - | - | - | - | $01 | - |
| $05: NorSpr41_UnknownSprite_State5 [table-derived] | - | - | - | - | $01 | - |
| $06: NorSpr41_UnknownSprite_State6 [table-derived] | - | - | - | - | $07 | - |
| $07: NorSpr41_UnknownSprite_State7 [table-derived] | - | - | - | - | - | - |
| $08: NorSpr41_UnknownSprite_State8 [table-derived] | - | - | GainLife | - | - | - |

## DKC1_NorSpr15_BananaBunch_Main  (dispatch `0xBFD731`, 9 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr15_BananaBunch_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr15_BananaBunch_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr15_BananaBunch_State2 [table-derived] | - | - | CollectBananaBunch | - | - | - |
| $03: NorSpr15_BananaBunch_State3 [table-derived] | - | - | - | - | - | - |
| $04: NorSpr15_BananaBunch_State4 [table-derived] | - | - | - | - | - | - |
| $05: NorSpr15_BananaBunch_State5 [table-derived] | - | - | - | - | - | - |
| $06: NorSpr15_BananaBunch_State6 [table-derived] | - | - | - | - | - | - |
| $07: NorSpr15_BananaBunch_State7 [table-derived] | - | - | - | - | - | - |
| $08: NorSpr15_BananaBunch_State8 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr20_Mincer_Main  (dispatch `0xBFD9DA`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr20_Mincer_State0 [table-derived] | - | Zinger_Dead | - | - | $01 | - |
| $01: NorSpr20_Mincer_State1 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr10_NeckyNut_Main  (dispatch `0xBFDA28`, 3 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr10_NeckyNut_State0 [table-derived] | - | - | - | - | $01 | - |
| $01: NorSpr10_NeckyNut_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr10_NeckyNut_State2 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr1B_HalfTire_Main  (dispatch `0xBFDCC2`, 3 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr1B_HalfTire_State0 [table-derived] | - | HalfTire_Bounce, RollingTire_Bounce | TireBounce | $80 | $01 | - |
| $01: NorSpr1B_HalfTire_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr1B_HalfTire_State2 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr21_UnknownSprite_Main  (dispatch `0xBFDDC1`, 1 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr21_UnknownSprite_State0 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr1D_RollingTire_Main  (dispatch `0xBFDE51`, 5 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr1D_RollingTire_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr1D_RollingTire_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr1D_RollingTire_State2 [table-derived] | - | - | - | - | $03 | - |
| $03: NorSpr1D_RollingTire_State3 [table-derived] | - | - | - | - | - | - |
| $04: NorSpr1D_RollingTire_State4 [table-derived] | - | RollingTire_Unknown | - | - | - | - |

## DKC1_NorSpr1A_Klaptrap_Main  (dispatch `0xBFE062`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr1A_Klaptrap_State0 [table-derived] | - | Klaptrap_Dead, Klaptrap_Walk | - | - | $01 | - |
| $01: NorSpr1A_Klaptrap_State1 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr19_Zinger_Main  (dispatch `0xBFE0D4`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr19_Zinger_State0 [table-derived] | - | Zinger_Dead | - | - | $01 | - |
| $01: NorSpr19_Zinger_State1 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr2C_ItemCache_Main  (dispatch `0xBFE126`, 3 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr2C_ItemCache_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr2C_ItemCache_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr2C_ItemCache_State2 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr38_BarrelCannon_Main  (dispatch `0xBFE34A`, 12 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr38_BarrelCannon_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr38_BarrelCannon_State1 [table-derived] | - | - | EnterBarrelCannon | $00 | $29 | - |
| $02: NorSpr38_BarrelCannon_State2 [table-derived] | - | - | - | - | $08, $0A | - |
| $03: NorSpr38_BarrelCannon_State3 [table-derived] | - | - | - | - | $02, $09 | - |
| $04: NorSpr38_BarrelCannon_State4 [table-derived] | - | - | ShootOutOfBarrelCannon | - | $28, $2A | - |
| $05: NorSpr38_BarrelCannon_State5 [table-derived] | - | - | - | - | - | - |
| $06: NorSpr38_BarrelCannon_State6 [table-derived] | - | - | - | - | $01 | - |
| $07: NorSpr38_BarrelCannon_State7 [table-derived] | - | - | - | - | - | - |
| $08: NorSpr38_BarrelCannon_State8 [table-derived] | - | - | - | - | - | - |
| $09: NorSpr38_BarrelCannon_State9 [table-derived] | - | - | - | - | - | - |
| $0A: NorSpr38_BarrelCannon_State10 [table-derived] | - | - | - | - | $0B | - |
| $0B: NorSpr38_BarrelCannon_State11 [table-derived] | - | - | - | - | $0A | - |

## DKC1_NorSpr39_SpritePlatform_Main  (dispatch `0xBFEA28`, 5 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr39_SpritePlatform_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr39_SpritePlatform_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr39_SpritePlatform_State2 [table-derived] | - | - | - | - | - | - |
| $03: NorSpr39_SpritePlatform_State3 [table-derived] | - | - | - | - | - | - |
| $04: NorSpr39_SpritePlatform_State4 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr31_SwingingRope_Main  (dispatch `0xBFEC65`, 4 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr31_SwingingRope_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr31_SwingingRope_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr31_SwingingRope_State2 [table-derived] | - | - | - | - | - | - |
| $03: NorSpr31_SwingingRope_State3 [table-derived] | - | - | - | - | $01 | - |

## DKC1_NorSpr30_VerticalRope_Main  (dispatch `0xBFEDF1`, 6 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr30_VerticalRope_State0 [table-derived] | - | - | - | - | $01, $04 | - |
| $01: NorSpr30_VerticalRope_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr30_VerticalRope_State2 [table-derived] | - | - | - | - | - | - |
| $03: NorSpr30_VerticalRope_State3 [table-derived] | - | - | - | - | - | - |
| $04: NorSpr30_VerticalRope_State4 [table-derived] | - | - | - | - | - | - |
| $05: NorSpr30_VerticalRope_State5 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr07_DiddysHat_Main  (dispatch `0xBFEF72`, 3 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr07_DiddysHat_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr07_DiddysHat_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr07_DiddysHat_State2 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr2F_Army_Main  (dispatch `0xBFEFC6`, 5 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr2F_Army_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr2F_Army_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr2F_Army_State2 [table-derived] | - | - | - | - | - | - |
| $03: NorSpr2F_Army_State3 [table-derived] | - | - | - | - | - | - |
| $04: NorSpr2F_Army_State4 [table-derived] | - | - | - | - | - | - |

## DKC1_NorSpr05_Kritter_Main  (dispatch `0xBFF0EB`, 4 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: NorSpr05_Kritter_State0 [table-derived] | - | - | - | - | - | - |
| $01: NorSpr05_Kritter_State1 [table-derived] | - | - | - | - | - | - |
| $02: NorSpr05_Kritter_State2 [table-derived] | - | - | - | - | - | - |
| $03: NorSpr05_Kritter_State3 [table-derived] | - | - | - | - | - | - |

## Player_CallStateRoutine  (dispatch `0xBFF17D`, 27 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: Player_CallStateRoutine_State0 [table-derived] | - | - | - | - | - | - |
| $01: Player_CallStateRoutine_State1 [table-derived] | - | - | - | - | - | - |
| $02: Player_CallStateRoutine_State2 [table-derived] | - | - | - | - | - | - |
| $03: Player_CallStateRoutine_State3 [table-derived] | - | - | - | - | - | - |
| $04: Player_CallStateRoutine_State4 [table-derived] | - | - | - | - | - | - |
| $05: Player_CallStateRoutine_State5 [table-derived] | - | - | - | - | - | - |
| $06: Player_CallStateRoutine_State6 [table-derived] | - | - | - | - | - | - |
| $07: Player_CallStateRoutine_State7 [table-derived] | - | - | - | - | - | - |
| $08: Player_CallStateRoutine_State8 [table-derived] | - | - | - | - | - | - |
| $09: Player_CallStateRoutine_State9 [table-derived] | - | - | - | - | - | - |
| $0A: Player_CallStateRoutine_State10 [table-derived] | - | - | - | - | - | - |
| $0B: Player_CallStateRoutine_State11 [table-derived] | - | - | - | - | - | - |
| $0C: Player_CallStateRoutine_State12 [table-derived] | - | - | - | - | - | - |
| $0D: Player_CallStateRoutine_State13 [table-derived] | - | - | - | - | - | - |
| $0E: Player_CallStateRoutine_State14 [table-derived] | - | - | - | - | - | - |
| $0F: Player_CallStateRoutine_State15 [table-derived] | - | - | - | - | - | - |
| $10: Player_CallStateRoutine_State16 [table-derived] | - | - | - | - | - | - |
| $11: Player_CallStateRoutine_State17 [table-derived] | - | - | - | - | - | - |
| $12: Player_CallStateRoutine_State18 [table-derived] | - | - | - | - | - | - |
| $13: Player_CallStateRoutine_State19 [table-derived] | - | - | - | - | - | - |
| $14: Player_CallStateRoutine_State20 [table-derived] | - | - | - | - | - | - |
| $15: Player_CallStateRoutine_State21 [table-derived] | - | - | - | - | - | - |
| $16: Player_CallStateRoutine_State22 [table-derived] | - | - | - | - | - | - |
| $17: Player_CallStateRoutine_State23 [table-derived] | - | - | - | - | - | - |
| $18: Player_CallStateRoutine_State24 [table-derived] | - | - | - | - | - | - |
| $19: Player_CallStateRoutine_State25 [table-derived] | - | - | - | - | - | - |
| $1A: Player_CallStateRoutine_State26 [table-derived] | - | - | - | - | - | - |

## Player_CallStateRoutine_State1  (dispatch `0xBFF268`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF26F | - | - | - | - | - | - |
| $01: CODE_BFF280 | - | - | - | - | - | - |

## Player_CallStateRoutine_State3  (dispatch `0xBFF301`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF308 | - | - | - | - | - | - |
| $01: CODE_BFF313 | - | Kritter_Jump | - | - | - | - |

## Player_CallStateRoutine_State4  (dispatch `0xBFF358`, 3 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF361 | - | - | - | - | - | - |
| $01: CODE_BFF36B | - | Kritter_Jump | - | - | - | - |
| $02: CODE_BFF3B2 | - | - | - | - | - | - |

## Player_CallStateRoutine_State6  (dispatch `0xBFF43F`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF446 | - | - | - | - | - | - |
| $01: CODE_BFF463 | - | Kritter_Walk | - | - | - | - |

## Player_CallStateRoutine_State7  (dispatch `0xBFF526`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF52D | - | - | - | - | - | - |
| $01: CODE_BFF541 | - | - | - | - | - | - |

## Player_CallStateRoutine_State8  (dispatch `0xBFF549`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF550 | - | - | - | - | - | - |
| $01: CODE_BFF570 | - | - | - | - | - | - |

## Player_CallStateRoutine_State9  (dispatch `0xBFF62F`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF636 | - | - | - | - | - | - |
| $01: CODE_BFF64D | - | - | - | - | - | - |

## Player_CallStateRoutine_State16  (dispatch `0xBFF67F`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF686 | - | - | - | - | - | - |
| $01: CODE_BFF6A6 | - | - | - | - | - | - |

## Player_CallStateRoutine_State17  (dispatch `0xBFF6B2`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF6B9 | - | - | - | - | - | - |
| $01: CODE_BFF6D0 | - | - | - | - | - | - |

## Player_CallStateRoutine_State18  (dispatch `0xBFF791`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF798 | - | - | - | - | - | - |
| $01: CODE_BFF7A9 | - | - | - | - | - | - |

## Player_CallStateRoutine_State19  (dispatch `0xBFF7DD`, 9 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF846 | - | - | - | - | - | - |
| $01: CODE_BFF847 | - | - | - | - | - | - |
| $02: CODE_BFF851 | - | - | - | - | - | - |
| $03: CODE_BFF862 | - | - | - | - | - | - |
| $04: CODE_BFF86C | - | - | - | - | - | - |
| $05: CODE_BFF87D | - | - | - | - | - | - |
| $06: CODE_BFF88E | - | - | - | - | - | - |
| $07: CODE_BFF8C5 | - | - | - | - | - | - |
| $08: CODE_BFF8F9 | - | - | - | - | - | - |

## Player_CallStateRoutine_State22  (dispatch `0xBFF93C`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF943 | - | - | - | - | - | - |
| $01: CODE_BFF95D | - | - | - | - | - | - |

## Player_CallStateRoutine_State25  (dispatch `0xBFF9AD`, 2 states, bankbf.cfg)

| ordinal / state | runtime | anim refs (static) | sound refs (static) | events→$1595 (static) | state stores→$1029 (static) | spawns (static) |
|---|---|---|---|---|---|---|
| $00: CODE_BFF9B4 | - | - | - | - | - | - |
| $01: CODE_BFF9C5 | - | - | - | - | - | - |
