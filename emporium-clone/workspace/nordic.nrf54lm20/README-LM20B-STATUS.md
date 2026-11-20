# nRF54LM20B EM Script distro — source bundle

This bundle is based on the uploaded `nordic.nrf54lm20_headers_interrupts.zip`.
It is NOT validated as flash-ready. Do not erase or program the DK based solely on this bundle.

## Included
- nRF54LM20B device headers in `nordic.distro.nrf54lm20/inc/`
- Updated `BuildC.em.ts`, `StartupC.em.ts`, `IntrC.em.ts`, `REGS.hpp.txt`, and `genregs.ts`
- Existing board/MCU/gist files and non-destructive J-Link connection script

## REQUIRED: regenerate register declarations
The uploaded ZIP contained an older `REGS.em.ts` (72 interfaces, 18 instances),
while your newer local regeneration produced 77 interfaces and 23 instances.
To prevent accidental use of stale declarations, the older generated file has
been OMITTED from this bundle. From the distro directory, run:

    ../../../node_modules/.bin/tsx genregs.ts

Then verify:

    grep -c '^export interface ' REGS.em.ts
    grep -Ec '^export const [A-Za-z0-9_]+ = \{\} as ' REGS.em.ts

Expected counts from your successful local regeneration: 77 and 23.
Keep your newer local `REGS.em.ts` if it already matches those counts.

## Still requiring verification
- Vector-table alignment (306 entries require 2048-byte alignment); the shared
  linker implementation in `em.target/em.build.segger/LinkerC.em.ts` is outside
  this distro bundle and has NOT been modified.
- Generated C++ peripheral register expressions and actual linker map / ELF.
- Startup hook ordering and boot/security attribution on physical nRF54LM20B-DK.
- Board pin assignments, UART, clocks, memory reservations, and J-Link load procedure.
- `NRF_FICR_S` alias and peripheral security attribution.

## Integration
Extract the `nordic.nrf54lm20` directory into your emporium `workspace/`.
Back up your current distro before replacing files. Re-run the register generator
and then build a minimal test application before attempting to flash hardware.
