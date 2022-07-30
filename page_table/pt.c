#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "os.h"

/*configuration constants*/
#define va_offset_size 12
#define va_unused_bits_size 7
#define ppn_unused_bits_size 11
#define trie_key_size 9
#define nof_pt_nodes (64 - va_offset_size - va_unused_bits_size)/trie_key_size
#define key_mask ((1<<trie_key_size)-1)

/*usefull macro*/
#define is_pte_valid(pte) ((pte&1) == 1)



void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn) {
	uint64_t* va = (uint64_t*)phys_to_virt(pt << 12); /*va which points to the base of the first pt node*/
	assert(va != NULL);
	for (int i = 0; i < nof_pt_nodes-1; i++){
		va = va + ((vpn>>(45 - (i+1)*trie_key_size)) & key_mask); /*we consider only the bottom 45 bits of vpn*/
		if (!(*va)) { /*page is not allocated*/
			if (ppn == NO_MAPPING) {/*destroying an unmapped page --> do nothing*/
				return;
			}
			uint64_t page_num = alloc_page_frame();
			*va = (page_num << (ppn_unused_bits_size + 1)) + 1; /*update the ppn(with valid = 1)*/
		}
		/*move to next node*/
		va = (uint64_t*)phys_to_virt((*va) & 0xFFFFFFFFFFFFF000);
		assert(va != NULL);
	}
	/*update the desire entry*/
	va = va + (vpn & key_mask);
	*va = (ppn == NO_MAPPING) ? 2 : (ppn << (ppn_unused_bits_size + 1)) + 1; /*if we wish to destroy a mapping - 2 does the trick (valid is 0)*/
}

uint64_t page_table_query(uint64_t pt, uint64_t vpn) {
	uint64_t* va = (uint64_t*)phys_to_virt(pt << 12); /*va which points to the base of the first pt node*/
	assert(va != NULL);
	for (int i = 0; i < nof_pt_nodes-1; i++) {
		va = va + ((vpn >> (45 - (i + 1)*trie_key_size)) & key_mask);/*we consider only the bottom 45 bits of vpn*/
		if (!is_pte_valid(*va)) {
			return NO_MAPPING;
		}
		va = (uint64_t*)phys_to_virt((*va)&0xFFFFFFFFFFFFF000);
		assert(va != NULL);
	}
	va = va + (vpn & key_mask);
	return (is_pte_valid(*va)) ? ((*va) >> (ppn_unused_bits_size + 1)) : NO_MAPPING;
}
