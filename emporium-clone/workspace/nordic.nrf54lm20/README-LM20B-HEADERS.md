# LM20B header and interrupt update

Updated Nordic LM20B application/types/global headers, REGS.hpp.txt include references, GPIO P3 selection, and IntrC.em.ts from the exact IRQn_Type values.

NOT FLASH READY: REGS.em.ts remains generated for nRF54L05 and must be regenerated using the LM20B headers and EM Script register generation workflow. BoardC/StartupC and boot/security behavior require validation. No flashing has been enabled.

Header dependencies include core_cm33.h, system_nrf.h and compiler_abstraction.h; confirm availability in the build include paths. IRQ vector generator must preserve null/reserved slots through IRQ 289.
