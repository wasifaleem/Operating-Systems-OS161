/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <coremap.h>
#include <spl.h>
#include <mips/tlb.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	as->pt_dir = kmalloc(sizeof(struct page_directory));
	if (as->pt_dir == NULL) {
		kfree(as);
		return NULL;
	}

	for (int i = 0; i < PAGE_TABLE_SIZE; ++i) {
		as->pt_dir->pt_table[i] = NULL;
	}

	as->segments = NULL;
	as->heap = NULL;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas == NULL) {
		return ENOMEM;
	}

	// copy segments, heap, stackptr
	if (old->segments != NULL) {
		newas->segments = kmalloc(sizeof(struct segment));
		if (newas->segments == NULL) {
			return ENOMEM;
		}
		*(newas->segments) = *(old->segments);
		struct segment *new_segment = newas->segments;
		struct segment *old_segment = old->segments->next_segment;
		while (old_segment != NULL) {
			new_segment->next_segment = kmalloc(sizeof(struct segment));
			if (new_segment->next_segment == NULL) {
				kfree(newas->segments);
				return ENOMEM;
			}
			new_segment = new_segment->next_segment;
			*(new_segment) = *(old_segment);
			old_segment = old_segment->next_segment;
		}
		new_segment->next_segment = NULL;
	}
	if (old->heap != NULL) {
		newas->heap = kmalloc(sizeof(struct segment));
		if (newas->heap == NULL) {
			return ENOMEM;
		}
		*(newas->heap) = *(old->heap);
	}

	// copy page dir & table & entries
	for (unsigned i = 0; i < PAGE_TABLE_SIZE; ++i) {
		struct page_table *pt = (old->pt_dir)->pt_table[i];
		if (pt == NULL) {
			(newas->pt_dir)->pt_table[i] = NULL;
		} else {
			(newas->pt_dir)->pt_table[i] = kmalloc(sizeof(struct page_table));
			if ((newas->pt_dir)->pt_table[i] == NULL) {
				return ENOMEM;
			}
			for (int j = 0; j < PAGE_TABLE_SIZE; ++j) {
				struct page_table_entry *pte = pt->pt_entries[j];
				if (pte == NULL) {
					((newas->pt_dir)->pt_table[i])->pt_entries[j] = NULL;
				} else {
					struct page_table_entry *new_pte = kmalloc(sizeof(struct page_table_entry));
					if (new_pte == NULL) {
						return ENOMEM;
					}
					if (pte->valid && pte->pbase != 0) {
						paddr_t new_pa = 0;
						if ((new_pa = single_page_alloc(USER)) == 0) {
							return ENOMEM;
						}
						new_pte->pbase = new_pa;
						memmove((void *) PADDR_TO_KVADDR(new_pte->pbase),
								(const void *) PADDR_TO_KVADDR(pte->pbase),
								PAGE_SIZE);
					}
					((newas->pt_dir)->pt_table[i])->pt_entries[j] = new_pte;
					new_pte->read = pte->read;
					new_pte->write = pte->write;
					new_pte->execute = pte->execute;
//					new_pte->state = pte->state;
					new_pte->valid = pte->valid;
//					new_pte->referenced = pte->referenced;
				}
			}
		}
	}

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	for (unsigned i = 0; i < PAGE_TABLE_SIZE; ++i) {
		struct page_table *pt = (as->pt_dir)->pt_table[i];
		if (pt != NULL) {
			for (int j = 0; j < PAGE_TABLE_SIZE; ++j) {
				struct page_table_entry *pte = pt->pt_entries[j];
				if (pte != NULL) {
					if (pte->valid && pte->pbase != 0) {
						free_kpages(PADDR_TO_KVADDR(pte->pbase));
					}
					kfree(pte);
				}
			}
			kfree(pt);
		}
	}

	struct segment *curr = as->segments;
	while (curr != NULL) {
		struct segment *temp = curr;
		curr = curr->next_segment;
		kfree(temp);
	}

	kfree(as->heap);
	kfree(as->pt_dir);
	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	vm_tlbshootdown_all();
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
				 int readable, int writeable, int executable)
{
	size_t npages;

	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t) PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = memsize / PAGE_SIZE;

	struct segment *new_segment = kmalloc(sizeof(struct segment));
	new_segment->next_segment = NULL;
	if (as->segments == NULL) {
		as->segments = new_segment;
		// alloc heap
		as->heap = kmalloc(sizeof(struct segment));
		as->heap->npages = 1;
		as->heap->next_segment = NULL;
		as->heap->read = 1;
		as->heap->write = 1;
		as->heap->execute = 0;
		as->heap->vstart = 0;
		as->heap->vend = 0;
	} else {
		struct segment *curr = as->segments;
		while (curr->next_segment != NULL) {
			curr = curr->next_segment;
		}
		curr->next_segment = new_segment;
	}
	new_segment->npages = npages;
	new_segment->read = (unsigned int) (readable > 0);
	new_segment->write = (unsigned int) (writeable > 0);
	new_segment->execute = (unsigned int) (executable > 0);

	new_segment->vstart = vaddr;
	new_segment->vend = vaddr + npages * PAGE_SIZE;
	if (new_segment->vend > as->heap->vstart) {
		as->heap->vstart = as->heap->vend = new_segment->vend + PAGE_SIZE;
	}

	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->segments != NULL);
	KASSERT(as->heap != NULL);
	struct segment *curr = as->segments;
	while (curr != NULL) {
		if (alloc_segment_pte(as->pt_dir, curr->vstart, curr->npages, UP, 1, 1, 1)) { // grant all for load_elf
			return ENOMEM;
		}
		curr = curr->next_segment;
	}

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	KASSERT(as->segments != NULL);
	KASSERT(as->heap != NULL);
	struct segment *curr = as->segments;
	while (curr != NULL) {
		alloc_segment_pte(as->pt_dir, curr->vstart, curr->npages, UP, curr->read, curr->write, curr->execute);
		curr = curr->next_segment;
	}
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	*stackptr = USERSTACK;
	(void)as;
	return 0;
}

