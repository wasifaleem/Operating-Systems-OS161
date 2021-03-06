#include <coremap.h>
#include <lib.h>
#include <cpu.h>
#include <synch.h>
#include <addrspace.h>

static struct cm_entry *coremap;
static unsigned int coremap_start_entry;
static unsigned int coremap_entry_count;
static volatile unsigned int coremap_free_entries;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

void cm_bootstrap(void)
{
	paddr_t last = ram_getsize();
	paddr_t first_free = ram_getfirstfree();

	unsigned int page_count = (last - first_free) / PAGE_SIZE;

	coremap = (struct cm_entry *) PADDR_TO_KVADDR(first_free);
	first_free = first_free + ROUNDUP(page_count * sizeof(struct cm_entry), PAGE_SIZE);

	coremap_start_entry = first_free / PAGE_SIZE;
	coremap_entry_count = coremap_free_entries = ((last - first_free) / PAGE_SIZE);

	for (unsigned i = 0; i < coremap_entry_count; i++) {
		coremap[i] = (struct cm_entry) {.page_count = 0, .state = FREE};
	}
}

vaddr_t alloc_kpages(unsigned npages)
{
	paddr_t pa = 0;
	if (npages == 1) {
		pa = single_page_alloc(KERNEL);
	} else if (npages > 1) {
		pa = multi_page_alloc(KERNEL, npages);
	}
	if (pa != 0) {
		return PADDR_TO_KVADDR(pa);
	}
	return pa;
}


void free_kpages(vaddr_t addr)
{
	paddr_t paddr = KVADDR_TO_PADDR(addr);
	unsigned int page_index = to_cm(paddr);
	if (page_index <= coremap_entry_count) {
		unsigned int chunk_size = coremap[page_index].page_count;
		KASSERT(chunk_size > 0);
		if (chunk_size > 0) {
			spinlock_acquire(&coremap_lock);
			if (chunk_size == 1) {
				cm_free_page(page_index);
			} else {
				for (unsigned int i = page_index; i < page_index + chunk_size; ++i) {
					cm_free_page(i);
				}
			}
			spinlock_release(&coremap_lock);
		}
	}
}

unsigned int coremap_used_bytes()
{
	return (coremap_entry_count - coremap_free_entries) * PAGE_SIZE;
}

paddr_t multi_page_alloc(page_type type, unsigned int npages)
{
	if (coremap_free_entries > npages) {
		spinlock_acquire(&coremap_lock);
		unsigned int chunk_index = 0, chunk_size = 0;
		for (unsigned int i = 0; i < coremap_entry_count; i++) {
			if (chunk_size == npages) {
				break;
			}
			bool free = (coremap[i].state == FREE);
			if (chunk_size <= 0 && free) {
				chunk_index = i;
				chunk_size = 1;
			} else if (chunk_size > 0 && free) {
				chunk_size++;
			} else if (chunk_size > 0) {
				chunk_size = 0;
			}
		}
		if (chunk_size == npages) {
			for (unsigned int i = chunk_index; i < chunk_index + chunk_size; ++i) {
				cm_allocate_page(i, type);
			}
			coremap[chunk_index].page_count = chunk_size;
			spinlock_release(&coremap_lock);
			return to_paddr(chunk_index);
		}

		spinlock_release(&coremap_lock);
	}
	return 0;
}

paddr_t single_page_alloc(page_type type)
{
	paddr_t pa = 0;
	if (coremap_free_entries >= 1) {
		spinlock_acquire(&coremap_lock);
		int free_entry_index = -1;
		for (unsigned i = 0; i < coremap_entry_count; i++) {
			if (coremap[i].state == FREE) {
				free_entry_index = i;
				break;
			}
		}
		if (free_entry_index >= 0) {
			pa = cm_allocate_page((unsigned int) free_entry_index, type);
			coremap[free_entry_index].page_count = 1;
		}
		spinlock_release(&coremap_lock);
	}
	return pa;
}

paddr_t cm_allocate_page(unsigned int free_entry_index, page_type type)
{
	KASSERT(spinlock_do_i_hold(&coremap_lock) == true);
	KASSERT(coremap[free_entry_index].state == FREE);

	switch (type) {
		case KERNEL: {
			coremap[free_entry_index].state = FIXED;
			break;
		}
		case USER: {
			coremap[free_entry_index].state = DIRTY;
			break;
		}
	}
	coremap_free_entries--;
	paddr_t paddr = to_paddr(free_entry_index);
	bzero((void *) PADDR_TO_KVADDR(paddr), PAGE_SIZE);
	return paddr;
}

void cm_free_page(unsigned int used_entry_index)
{
	KASSERT(spinlock_do_i_hold(&coremap_lock) == true);
	KASSERT(coremap[used_entry_index].state != FREE);

	coremap_free_entries++;
	coremap[used_entry_index] = (struct cm_entry) {.page_count = 0, .state = FREE};
}

paddr_t to_paddr(unsigned int coremap_index)
{
	return (coremap_start_entry + coremap_index) * PAGE_SIZE;
}

unsigned int to_cm(paddr_t pa)
{
	return (pa / PAGE_SIZE) - coremap_start_entry;
}