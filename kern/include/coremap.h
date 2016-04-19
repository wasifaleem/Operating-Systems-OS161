#ifndef COREMAP_H
#define COREMAP_H

#include <types.h>

typedef enum {
	FIXED, FREE, DIRTY, CLEAN
} page_state;

typedef enum {
	KERNEL, USER
} page_type;

struct cm_entry {
	page_state state;
	/* records chunk of consecutively allocated pages */
	unsigned int page_count;
};

void cm_bootstrap(void);

paddr_t to_paddr(unsigned int coremap_index);

unsigned int to_cm(paddr_t pa);

paddr_t single_page_alloc(page_type type);

paddr_t multi_page_alloc(page_type type, unsigned int npages);

paddr_t cm_allocate_page(unsigned int free_entry_index, page_type type);

void cm_free_page(unsigned int used_entry_index);
// TODO: make private

#endif //COREMAP_H
