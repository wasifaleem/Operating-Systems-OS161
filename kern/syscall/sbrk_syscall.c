#include <types.h>
#include <proc.h>
#include <syscall.h>
#include <lib.h>
#include <current.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <mips/tlb.h>
#include <spl.h>

int sys_sbrk(intptr_t amount, int *retval)
{
	*retval = -1;
	struct addrspace *as = proc_getas();
	KASSERT(as != NULL);
	KASSERT(as->heap != NULL);

	if (amount == 0) {
		*retval = as->heap->vend;
		return 0;
	}

	vaddr_t new_heap_vend = (as->heap->vend + (vaddr_t) amount) & PAGE_FRAME;

	if (new_heap_vend < as->heap->vstart || (amount <= (-4096 * 1024 * 256))) { // TODO: replace magic number!
		return EINVAL;
	}
	if (new_heap_vend >= (USERSTACK - STACKPAGES * PAGE_SIZE) || new_heap_vend > USERSPACETOP) {
		return ENOMEM;
	}

	if (new_heap_vend < as->heap->vend) {
		int size = ((as->heap->vend - new_heap_vend) & PAGE_FRAME) / PAGE_SIZE;
		for (int j = 0; j < size; ++j) {
			vaddr_t free = new_heap_vend + j * PAGE_SIZE;
			struct page_table_entry *pte = find_pte(as->pt_dir, free);
			if (pte != NULL) {
				if (pte->valid) {
					vaddr_t pvaddr = PADDR_TO_KVADDR(pte->pbase);
					free_kpages(pvaddr);
					pte->valid = 0;
					pte->pbase = 0;

					int spl = splhigh();
					int i = tlb_probe(free, 0);
					if (i > 0) {
						tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
					}
					splx(spl);
				}
			}
		}
	}
	*retval = as->heap->vend;
	as->heap->vend = new_heap_vend;
	return 0;
}