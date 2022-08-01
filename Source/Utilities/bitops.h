#pragma once

#define BITS_PER_LONG sizeof(LONG) * 8

#define GENMASK(h, l) (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define BIT(nr) (1UL << (nr))

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size));		\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

/* same as for_each_set_bit() but use bit as value to start with */
#define for_each_set_bit_from(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

#define for_each_clear_bit(bit, addr, size) \
	for ((bit) = find_first_zero_bit((addr), (size));	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

/* same as for_each_clear_bit() but use bit as value to start with */
#define for_each_clear_bit_from(bit, addr, size) \
	for ((bit) = find_next_zero_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

#ifdef __cplusplus
extern "C" {
#endif

unsigned long find_next_zero_bit(const unsigned long* addr, unsigned long size,
	unsigned long offset);
unsigned long find_next_bit(const unsigned long* addr, unsigned long size,
	unsigned long offset);
unsigned long find_first_bit(const unsigned long* addr, unsigned long size);

int fls(unsigned int x);
unsigned long __ffs(unsigned long word);
unsigned int __sw_hweight32(unsigned int w);

#define hweight_long __sw_hweight32

#ifdef __cplusplus
}
#endif