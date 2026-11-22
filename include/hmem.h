/* <hmem.h> Handles on memory outside the 64K data space (stackette).
 *
 * A process has a pool of pages (4K) that its data space cannot address, and a
 * window of a few pages in its data space, through which it sees the pages of
 * the pool that it has asked for. A block of the pool is a handle; hm_lock()
 * shows it in the window and gives its address, hm_unlock() lets the window
 * show something else. The address of a locked block is good until the unlock.
 * Blocks are whole pages: it is for the large objects, small ones are for malloc.
 */
#ifndef _HMEM_H
#define _HMEM_H

#define HM_PAGE		4096
#define HM_MAXWIN	8

typedef unsigned hm_t;		/* a handle; 0 is none */

int hm_init(unsigned _win_pages, unsigned _pool_pages);
hm_t hm_alloc(unsigned long _bytes);	/* the contents are not set */
hm_t hm_calloc(unsigned long _bytes);	/* zero */
hm_t hm_realloc(hm_t _h, unsigned long _bytes);	/* 0: failed, _h is as it was */
void hm_free(hm_t _h);
unsigned long hm_size(hm_t _h);
void *hm_lock(hm_t _h);		/* the whole block; NULL if it is not in the window */
void hm_unlock(hm_t _h);
void *hm_lock_range(hm_t _h, unsigned long _off, unsigned _len);
void hm_unlock_range(hm_t _h, unsigned long _off, unsigned _len);
unsigned hm_free_pages(void);	/* pages of the pool that are not in a block */

#endif
