#include <pagetable.h>
#include <lib.h>
#include <vm.h>
#include <kern/errno.h>

struct page_table_entry *find_pte(struct page_directory *pt_dir, vaddr_t vaddr)
{
	struct page_table *pt = pt_dir->pt_table[VADDR_TO_PD(vaddr)];
	if (pt == NULL) return NULL;
	struct page_table_entry *pte = pt->pt_entries[VADDR_TO_PT(vaddr)];
	return pte;
}

void free_pte(struct page_directory *pt_dir, vaddr_t vaddr)
{
	struct page_table_entry *pte = find_pte(pt_dir, vaddr);
	if (pte == NULL) return;
	if (pte->valid) {
		KASSERT(pte->pbase != 0);
		free_kpages(PADDR_TO_KVADDR(pte->pbase));
	}
	pt_dir->pt_table[VADDR_TO_PD(vaddr)] = NULL;
	kfree(pte);
}

int alloc_segment_pte(struct page_directory *pt_dir, vaddr_t vaddr, size_t npages, grow_direction_t grow, unsigned read,
					  unsigned write, unsigned execute)
{
	vaddr_t curr = vaddr;
	for (size_t i = 0; i < npages; ++i) {
		struct page_table *pt = pt_dir->pt_table[VADDR_TO_PD(curr)];
		if (pt == NULL) {
			pt = kmalloc(sizeof(struct page_table));
			if (pt == NULL) {
				return ENOMEM;
			}
			for (int j = 0; j < PAGE_TABLE_SIZE; ++j) {
				pt->pt_entries[j] = NULL;
			}
			pt_dir->pt_table[VADDR_TO_PD(curr)] = pt;
		}

		struct page_table_entry *pte = pt->pt_entries[VADDR_TO_PT(curr)];
		if (pte == NULL) {
			pte = kmalloc(sizeof(struct page_table_entry));
			if (pte == NULL) {
				return ENOMEM;
			}
			pt->pt_entries[VADDR_TO_PT(curr)] = pte;
			pte->valid = 0;
//			pte->state = 0;
			pte->pbase = 0;
		}
		pte->read = read;
		pte->write = write;
		pte->execute = execute;

		switch (grow) {
			case UP: {
				curr += PAGE_SIZE;
				break;
			}
			case DOWN: {
				curr -= PAGE_SIZE;
				break;
			}
		}
	}
	return 0;
}