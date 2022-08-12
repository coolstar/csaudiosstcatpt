#include "definitions.h"
#include "hw.h"
#include "resource.h"

#define IORESOURCE_BUSY		0x80000000	/* Driver has marked this resource busy */

/* Return the conflict entry if you can't request it */
static PRESOURCE __request_resource(PRESOURCE root, PRESOURCE newRes)
{
	size_t start = newRes->start;
	size_t end = newRes->end;
	PRESOURCE tmp, * p;

	if (end < start)
		return root;
	if (start < root->start)
		return root;
	if (end > root->end)
		return root;
	p = &root->child;
	for (;;) {
		tmp = *p;
		if (!tmp || tmp->start > end) {
			newRes->sibling = tmp;
			*p = newRes;
			newRes->parent = root;
			return NULL;
		}
		p = &tmp->sibling;
		if (tmp->end < start)
			continue;
		return tmp;
	}
}

/**
 * __request_region - create a new busy resource region
 * @parent: parent resource descriptor
 * @start: resource start address
 * @n: resource region size
 * @name: reserving caller's ID string
 * @flags: IO resource flags
 */
PRESOURCE __request_region(PRESOURCE parent,
	size_t start, size_t n, int flags)
{
	PRESOURCE res = (PRESOURCE)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(_RESOURCE), CSAUDIOCATPTSST_POOLTAG);
	if (!res)
		return NULL;

	res->start = start;
	res->end = start + n - 1;
	res->flags = IORESOURCE_BUSY;
	res->flags |= flags;

	for (;;) {
		PRESOURCE conflict;

		conflict = __request_resource(parent, res);
		if (!conflict)
			break;
		if (conflict != parent) {
			parent = conflict;
			if (!(conflict->flags & IORESOURCE_BUSY))
				continue;
		}
		/* Uhhuh, that didn't work out.. */
		ExFreePoolWithTag(res, CSAUDIOCATPTSST_POOLTAG);
		res = NULL;
		break;
	}
	return res;
}

void __release_region(PRESOURCE parent, size_t start,
	size_t n)
{
	PRESOURCE* p;
	size_t end;

	p = &parent->child;
	end = start + n - 1;

	for (;;) {
		PRESOURCE res = *p;

		if (!res)
			break;
		if (res->start <= start && res->end >= end) {
			if (!(res->flags & IORESOURCE_BUSY)) {
				p = &res->child;
				continue;
			}
			if (res->start != start || res->end != end)
				break;
			*p = res->sibling;
			ExFreePoolWithTag(res, CSAUDIOCATPTSST_POOLTAG);
			return;
		}
		p = &res->sibling;
	}

	CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Trying to free nonexistent resource "
		"<%016llx-%016llx>\n", (unsigned long long)start,
		(unsigned long long)end);
}

NTSTATUS release_resource(PRESOURCE old)
{
	PRESOURCE tmp, * p;

	p = &old->parent->child;
	for (;;) {
		tmp = *p;
		if (!tmp)
			break;
		if (tmp == old) {
			*p = tmp->sibling;
			old->parent = NULL;
			return STATUS_SUCCESS;
		}
		p = &tmp->sibling;
	}
	return STATUS_INVALID_PARAMETER;
}