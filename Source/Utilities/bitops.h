#pragma once

#define BITS_PER_LONG sizeof(LONG) * 8

#define GENMASK(h, l) (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define BIT(nr) (1UL << (nr))

#ifdef __cplusplus
extern "C" {
#endif

int fls(unsigned int x);
unsigned long __ffs(unsigned long word);
unsigned int __sw_hweight32(unsigned int w);

#define hweight_long __sw_hweight32

#ifdef __cplusplus
}
#endif