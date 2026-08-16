# SuperZSNES v0.230 state exporter

This tool converts DKC SuperZSNES `.szst*` files into a versioned, inspectable
bundle. It is the first half of the recomp snapshot bridge: SuperZSNES's five
typed machine-state records become JSON and its raw memories become exact
binary files.

The exporter intentionally fails closed:

- only the expected v0.230 DKC state classes are allowed during deserialization;
- the object order must be Master, 65816, SPC700, PPU, then DSP;
- exactly 280,640 raw bytes must follow those objects;
- special-chip and MSU-1 tails are rejected until explicitly modeled;
- source and every output file receive SHA-256 evidence.

Build and run:

```powershell
dotnet build .\tools\SuperZSNESStateExporter\SuperZSNESStateExporter.csproj -c Release
& .\tools\SuperZSNESStateExporter\bin\Release\net472\SuperZSNESStateExporter.exe `
  --input 'D:\Downloads\DKC_Widescreen_358x224.szst5' `
  --managed-dir 'D:\Downloads\SuperZSNES_v0.230\SUPERZSNES_Data\Managed' `
  --output '.\build\imported-states\state5' `
  --overwrite
```

`manifest.json` labels the result `complete-source-state`. That means the
bundle contains every subsystem SuperZSNES itself restores. It does **not**
mean a destination runtime's mapping is exact. The DKC1 recomp importer maps
the complete CPU/PPU/memory state and reconstructs DSP interpolation history;
therefore its first audio buffer is intentionally classified as reconstructed.
Gameplay promotion still requires deterministic replay comparison.

SuperZSNES indexes `io-registers.bin` as `(SNES address - $2000)`: `$2100`
is offset `$0100`, `$4200` is `$2200`, and `$4300` is `$2300`. Importers must
not use the register's low 12 bits; that mistake reads zero for NMITIMEN and
leaves a valid WAI-boundary save permanently frozen.

Do not deserialize untrusted `.szst` files with SuperZSNES itself or a general
BinaryFormatter utility. This exporter uses an exact class allowlist and size
checks, but the safest exchange format remains its emitted portable bundle.
