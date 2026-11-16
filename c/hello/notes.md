Change iteration number:
Select-String -Path "build/coremark/zephyr/.config" -Pattern "CONFIG_COREMARK_ITERATIONS"
Select-String -Path "build/coremark_cpuflpr/zephyr/.config" -Pattern "CONFIG_COREMARK_ITERATIONS"
