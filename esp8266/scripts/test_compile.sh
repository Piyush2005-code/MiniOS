#!/bin/bash
# test_compile.sh — verify all Checkpoint 1 sources compile clean
# Run from:  esp8266/  directory
# Usage:     bash scripts/test_compile.sh

set -e

GCC="/opt/xtensa-lx106-elf-gcc/bin/xtensa-lx106-elf-gcc"
INC="include"
SDK="include/sdk"

CFLAGS=(
    -Os
    -std=c11
    -mlongcalls
    -mtext-section-literals
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -DUSE_ESP_SDK
    -DARCH_ESP8266
    "-DICACHE_FLASH_ATTR=__attribute__((section(\".irom0.text\")))"
    "-DIRAM_ATTR=__attribute__((section(\".iram0.text\")))"
    -I"$INC"
    -I"$SDK"
    -I.           # exposes user_config.h in esp8266/ root
    -I"$INC/sdk"  # redundant with $SDK but harmless
)

mkdir -p build/hal build/kernel build/onnx build/net

SRCS=(
    "src/main.c:build/main.o"
    "src/hal/uart_esp.c:build/hal/uart_esp.o"
    "src/hal/timer_esp.c:build/hal/timer_esp.o"
    "src/hal/wifi_esp.c:build/hal/wifi_esp.o"
    "src/kernel/kmem_esp.c:build/kernel/kmem_esp.o"
    "src/kernel/sched_esp.c:build/kernel/sched_esp.o"
    "src/kernel/shell_esp.c:build/kernel/shell_esp.o"
)

PASS=0
FAIL=0

for entry in "${SRCS[@]}"; do
    src="${entry%%:*}"
    obj="${entry##*:}"
    printf "  CC  %-45s" "$src"
    if "$GCC" "${CFLAGS[@]}" -c "$src" -o "$obj" 2>/tmp/cc_err.txt; then
        echo "OK"
        ((PASS++)) || true
    else
        echo "FAILED"
        cat /tmp/cc_err.txt | head -20
        ((FAIL++)) || true
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
