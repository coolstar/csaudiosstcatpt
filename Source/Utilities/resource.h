#include "hw.h"
#pragma once

PRESOURCE __request_region(PRESOURCE parent,
	size_t start, size_t n, int flags);
void __release_region(PRESOURCE parent, size_t start,
	size_t n);
NTSTATUS release_resource(PRESOURCE old);