#ifndef PAGETABLE_H
#define PAGETABLE_H

#include <coremap.h>

#define PAGE_TABLE_SIZE 1024
#define VADDR_TO_PD(vaddr) ((vaddr) >> 22)
#define VADDR_TO_PT(vaddr) ((vaddr & 0x003FFFFF) >> 12)

struct page_table_entry {
	paddr_t pbase;
	unsigned read:1;
	unsigned write:1;
	unsigned execute:1;
//	unsigned state:1;
	unsigned valid:1;
//	unsigned referenced:1;
};

/* Second level */
struct page_table {
	struct page_table_entry *pt_entries[PAGE_TABLE_SIZE];
};

/* First level */
struct page_directory {
	struct page_table *pt_table[PAGE_TABLE_SIZE];
};

struct page_table_entry *find_pte(struct page_directory *pt_dir, vaddr_t vaddr);
void free_pte(struct page_directory *pt_dir, vaddr_t vaddr);

typedef enum {
	UP, DOWN
} grow_direction_t;

int alloc_segment_pte(struct page_directory *pt_dir, vaddr_t vaddr, size_t npages,
					  grow_direction_t grow, unsigned read, unsigned write, unsigned execute);

#endif //PAGETABLE_H
