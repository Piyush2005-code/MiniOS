sed -i '' '/void \*memcpy/a\
\    if ((uintptr_t)dest >= 0x100000000ULL || (uintptr_t)src >= 0x100000000ULL || n > 0x10000000) {\
\        HAL_UART_PutString("BAD MEMCPY! dest: "); bench_dec_w((uint32_t)((uintptr_t)dest>>32), 1); bench_dec_w((uint32_t)dest, 1); HAL_UART_PutString(" src: "); bench_dec_w((uint32_t)((uintptr_t)src>>32), 1); bench_dec_w((uint32_t)src, 1); HAL_UART_PutString(" n: "); bench_dec_w((uint32_t)n, 1); HAL_UART_PutString("\\n");\
\    }\
' src/lib/string.c
