#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>

static uint32_t tlb_index = 0;
static struct spinlock tlb_lock = SPINLOCK_INITIALIZER;

void vm_bootstrap(void)
{
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->segments != NULL);
	KASSERT(as->segments->vstart != 0);
	KASSERT((as->segments->vstart & PAGE_FRAME) == as->segments->vstart);

	KASSERT(as->heap != NULL);
	KASSERT(as->heap->vstart != 0);
	KASSERT((as->heap->vstart & PAGE_FRAME) == as->heap->vstart);

	unsigned read = 0, write = 0, execute = 0;
	grow_direction_t direction = UP;

	struct segment *curr = as->segments;
	bool valid = false;
	while (curr != NULL) {
		if ((faultaddress >= curr->vstart && faultaddress < curr->vend)) {
			valid = true;
			read = curr->read;
			write = curr->write;
			execute = curr->execute;
			break;
		}
		curr = curr->next_segment;
	}
	if (!valid && faultaddress >= as->heap->vstart && faultaddress < as->heap->vend) {
		read = write = execute = 1;
		valid = true;
	}
	if (!valid && faultaddress >= (USERSTACK - STACKPAGES * PAGE_SIZE) && faultaddress < USERSTACK) {
		read = write = execute = 1;
		direction = DOWN;
		valid = true;
	}
	if (!valid) {
		return EFAULT;
	}

	struct page_table_entry *pte = find_pte(as->pt_dir, faultaddress);
	if (pte == NULL) {
		if (alloc_segment_pte(as->pt_dir, faultaddress, 1, direction, read, write, execute)) {
//			free_pte(as->pt_dir, faultaddress);
			return ENOMEM;
		}
	}
	pte = find_pte(as->pt_dir, faultaddress);
	KASSERT(pte != NULL);
	if (!pte->valid) {
		if ((pte->pbase = single_page_alloc(USER)) == 0) {
//			kfree(pte);
			return ENOMEM;
		}
		pte->valid = 1;
	}
	KASSERT(pte != NULL);
	KASSERT(pte->pbase != 0);

	switch (faulttype) {
		case VM_FAULT_READ: {
			if (!pte->read) {
				return EFAULT;
			}
			break;
		}
		case VM_FAULT_READONLY: {
			return EFAULT;
		}
		case VM_FAULT_WRITE: {
			if (!pte->write) {
				return EFAULT;
			}
			break;
		}
		default:
			return EINVAL;
	}

	spinlock_acquire(&tlb_lock);
	spl = splhigh();
	paddr_t paddr = (pte->pbase);
//
//	for (int i=0; i<NUM_TLB; i++) {
//		tlb_read(&ehi, &elo, i);
//		if (elo & TLBLO_VALID) {
//			continue;
//		}
//		ehi = faultaddress;
//		elo = paddr | TLBLO_VALID;
//		if (pte->write) {
//			elo |= TLBLO_DIRTY;
//		}
//		tlb_write(ehi, elo, i);
//		splx(spl);
//		spinlock_release(&tlb_lock);
//		return 0;
//	}


	ehi = faultaddress;
	elo = paddr | TLBLO_VALID;
	if (pte->write) {
		elo |= TLBLO_DIRTY;
	}
	tlb_write(ehi, elo, tlb_index);
	tlb_index = (tlb_index + 1) % NUM_TLB;
	splx(spl);
	spinlock_release(&tlb_lock);

	return 0;
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void)
{
	spinlock_acquire(&tlb_lock);

	// Invalidate TLB.
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	for (int i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
	spinlock_release(&tlb_lock);
}

void vm_tlbshootdown(const struct tlbshootdown *tlbs)
{
	(void) tlbs;
//	int spl = splhigh();
//
//
//	splx(spl);
}