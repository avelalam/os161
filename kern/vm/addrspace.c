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
#include <cpu.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <mips/tlb.h>
#include <current.h>
#include <spl.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/stat.h>
#include <kern/fcntl.h>
#include <uio.h>
#include <bitmap.h>
#include <synch.h>
#include <kern/time.h>
#include <clock.h>


/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct spinlock cm_spinlock;

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	struct segment *heap = kmalloc(sizeof(struct segment));
	if(heap == NULL){
		return NULL;
	}
	heap->vbase = 0x400000;
	heap->vend = 0x400000;
	as->heap = heap;

	struct segment *stack = kmalloc(sizeof(struct segment));
	if(stack == NULL){
		return NULL;
	}
	stack->vend = USERSTACK;
	stack->vbase = stack->vend - 1024*PAGE_SIZE;
	as->stack = stack;

	as->segment_table = NULL;
	as->page_table = NULL;
	as->page_table_lock = lock_create("page_table");
	if(as->page_table_lock == NULL){
		return NULL;
	}	

	/*
	 * Initialize as needed.
	 */

	return as;
}

static
int page_table_copy(struct addrspace *oldas, struct addrspace *newas){

	if(swap_enabled != true){
		struct pte *old = oldas->page_table;

		for(;old!=NULL; old = old->next){

			struct pte *new_pte = page_table_add(newas, old->vaddr);
			if(new_pte == NULL){
				return ENOMEM;
			}
			memmove((void*)PADDR_TO_KVADDR(new_pte->paddr),
				(const void*)PADDR_TO_KVADDR(old->paddr),
				PAGE_SIZE);
		}

		return 0;
	}else{
		struct pte *old = oldas->page_table;

		for(;old!=NULL; old=old->next){
			struct pte *new_pte = kmalloc(sizeof(struct pte));
			new_pte->vaddr = old->vaddr;
			new_pte->state = DISK;
			new_pte->pte_lock = lock_create("pte_lock");
			lock_acquire(bm_lock);
			int err = bitmap_alloc(swap_table, &new_pte->disk_slot);
			if(err){
				return ENOSPC;
			}
			lock_release(bm_lock);
			new_pte->next = NULL;
			
			lock_acquire(old->pte_lock);
			if(old->state == INMEMORY){
				write_to_disk(PADDR_TO_KVADDR(old->paddr), new_pte->disk_slot);
			}else{
				swapin(old);
				write_to_disk(PADDR_TO_KVADDR(old->paddr), new_pte->disk_slot);
				KASSERT(coremap[old->paddr/PAGE_SIZE].page_state == VICTIM);
				coremap[old->paddr/PAGE_SIZE].page_state = USER;

			}
			lock_release(old->pte_lock);

			
			// struct pte *new_pte = page_table_add(newas, old->vaddr);
			// if(new_pte == NULL){
			// 	return ENOMEM;
			// }			
			// lock_acquire(old->pte_lock);
			// if(old->state == INMEMORY){
			// 	memmove((void*)PADDR_TO_KVADDR(new_pte->paddr),
			// 				(const void*)PADDR_TO_KVADDR(old->paddr),
			// 					PAGE_SIZE);
			// }else{
			// 	read_from_disk(PADDR_TO_KVADDR(new_pte->paddr),old->disk_slot);
			// }
			// lock_release(old->pte_lock);
			// KASSERT(coremap[new_pte->paddr/PAGE_SIZE].page_state == VICTIM);
			// coremap[new_pte->paddr/PAGE_SIZE].page_state = USER;

			if(newas->page_table == NULL){
				newas->page_table = new_pte;
			}else{
				new_pte->next = newas->page_table;
				newas->page_table = new_pte;
			}
		}

		// for(;old!=NULL; old = old->next){
		// 	struct pte *new_pte = page_table_add(newas, old->vaddr);
		// 	if(new_pte == NULL){
		// 		return ENOMEM;
		// 	}
		// 	lock_acquire(old->pte_lock);
		// 	KASSERT(new_pte->state == INMEMORY);
		// 	if(old->state == INMEMORY){
		// 		memmove((void*)PADDR_TO_KVADDR(new_pte->paddr),
		// 					(const void*)PADDR_TO_KVADDR(old->paddr),
		// 						PAGE_SIZE);
		// 	}else{
		// 		lock_acquire(bm_lock);
		// 		KASSERT(old->state == DISK);
		// 		read_from_disk(PADDR_TO_KVADDR(new_pte->paddr), old->disk_slot);
		// 		lock_release(bm_lock);
		// 	}
			// KASSERT(coremap[new_pte->paddr/PAGE_SIZE].page_state == DIRTY);
			// coremap[new_pte->paddr/PAGE_SIZE].page_state = USER;
			// lock_release(new_pte->pte_lock);
			// lock_release(old->pte_lock);
		// }
		return 0;
	}

	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;
	(void)old;
	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */
	struct segment *currseg = old->segment_table;

	vaddr_t vaddr;
	size_t memsize;
	int readable = 0, writeable = 0, executable = 0;
	while(currseg != NULL){
		vaddr = currseg->vbase;
		memsize = currseg->vend - currseg->vbase;
		
		as_define_region(newas, vaddr, memsize, 
					readable, writeable, executable);

		currseg = currseg->next;
	}

	currseg = old->heap;
	newas->heap->vbase = currseg->vbase;
	newas->heap->vend = currseg->vend;
	

	currseg = old->stack;
	newas->stack->vbase = currseg->vbase;
	newas->stack->vend = currseg->vend;

	// int err = page_table_copy(old->page_table, &newas->page_table);
	// if(err){
	// 	return err;
	// }

	int err = page_table_copy(old, newas);
	if(err){
		return err;
	}

	*ret = newas;
	return 0;
}

static
void segment_table_destroy(struct addrspace *as){

	struct segment *segment_table = as->segment_table;
	struct segment *currseg;

	while(segment_table != NULL){
		currseg = segment_table;
		segment_table = segment_table->next;
		kfree(currseg);
	}

	kfree(as->heap);
	kfree(as->stack);
	(void)as;
}

static
void page_table_destroy(struct addrspace *as){

	if(swap_enabled != true){
		struct pte *page_table = as->page_table;
		struct pte *currpage;
		while(page_table != NULL){
			currpage = page_table;
			page_table = page_table->next;
			if(currpage->state == INMEMORY){
				free_upage(currpage->paddr);
			}else{
				// Clear the slot in the disk
				lock_acquire(bm_lock);
				bitmap_unmark(swap_table, currpage->disk_slot);
				lock_release(bm_lock);
			}
			lock_destroy(currpage->pte_lock);
			kfree(currpage);
		}
	}else{
		struct pte *page_table = as->page_table;
		struct pte *currpage;
		while(page_table != NULL){
			currpage = page_table;
			page_table = page_table->next;
			lock_acquire(currpage->pte_lock);
			if(currpage->state == INMEMORY){
				spinlock_acquire(&cm_spinlock);
				if(coremap[currpage->paddr/PAGE_SIZE].page_state == USER){
					free_upage(currpage->paddr);
				}else if(coremap[currpage->paddr/PAGE_SIZE].page_state == VICTIM){
					currpage->state = 	DESTROY;
					spinlock_release(&cm_spinlock);
					lock_release(currpage->pte_lock);
					continue;
				}
			}else{
				lock_acquire(bm_lock);
				bitmap_unmark(swap_table, currpage->disk_slot);
				lock_release(bm_lock);
			}
			lock_release(currpage->pte_lock);
			lock_destroy(currpage->pte_lock);
			kfree(currpage);
		}
	}
	(void)as;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

	page_table_destroy(as);
	segment_table_destroy(as);
	lock_destroy(as->page_table_lock);

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

	/*
	 * Write this.
	 */
	// kprintf("as activated\n");
	int spl;
	spl = splhigh();
	for(int i=0; i<NUM_TLB; i++){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
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
	/*
	 * Write this.
	 */

	(void)as;
	(void)vaddr;
	(void)memsize;
	(void)readable;
	(void)writeable;
	(void)executable;


	struct segment *new_segment = kmalloc(sizeof(struct segment));
	if(new_segment == NULL){
		return ENOMEM;
	}

	new_segment->vbase = vaddr;
	new_segment->vend = vaddr + memsize;
	new_segment->next = NULL;

	struct segment *curr = as->segment_table;
	while(curr != NULL && curr->next != NULL){
		curr = curr->next;
	}
	if(curr == NULL){
		as->segment_table = new_segment;
	}else{
		curr->next = new_segment;
	}

	as->heap->vend = new_segment->vend + PAGE_SIZE - (new_segment->vend % PAGE_SIZE);
	as->heap->vbase = as->heap->vend;
	
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}


/* Create new PTE for given vaddr and  
add it to the given process page_table*/

struct pte* page_table_add(struct addrspace *as, vaddr_t vaddr){

	if(swap_enabled != true){
		struct pte *new_pte = kmalloc(sizeof(struct pte));
		if(new_pte == NULL){
			// kprintf("pte is null\n");
			return NULL;
		}
		new_pte->vaddr = vaddr&PAGE_FRAME;
		paddr_t paddr = alloc_upage(new_pte);
		if(paddr == 0){
			kfree(new_pte);
			// kprintf("alloc_upage returned 0");
			return NULL;
		}
		new_pte->paddr = paddr;
		// new_pte->vpn = VPN(vaddr);
		// new_pte->ppn = paddr/PAGE_SIZE;
		new_pte->state = INMEMORY;
		new_pte->next = NULL;
		new_pte->pte_lock = lock_create("pte_lock");
		if(new_pte->pte_lock == NULL){
			kfree(new_pte);
			return NULL;
		}

		if(as->page_table == NULL){
			as->page_table = new_pte;
		}else{
			new_pte->next = as->page_table;
			as->page_table = new_pte;
		}

		return new_pte;
	}else{
		struct pte *new_pte = kmalloc(sizeof(struct pte));
		if(new_pte == NULL){
			// kprintf("pte is null\n");
			return NULL;
		}
		new_pte->vaddr = vaddr&PAGE_FRAME;
		// new_pte->vpn = VPN(vaddr);
		new_pte->state = INMEMORY;
		new_pte->next = NULL;
		new_pte->pte_lock = lock_create("pte_lock");
		if(new_pte->pte_lock == NULL){
			kfree(new_pte);
			kprintf("error creating lock\n");
			return NULL;
		}
		
		unsigned disk_slot;
		lock_acquire(bm_lock);
		int err = bitmap_alloc(swap_table, &disk_slot);
		lock_release(bm_lock);
		if(err){
			kprintf("bitmap error\n");
			return NULL;
		}
		new_pte->disk_slot = disk_slot;

		paddr_t paddr = alloc_upage(new_pte);
		if(paddr == 0){
			kprintf("alloc_upage returned 0");
			return NULL;
		}
		new_pte->paddr = paddr;
		// new_pte->ppn = paddr/PAGE_SIZE;
		if(as->page_table == NULL){
			as->page_table = new_pte;
		}else{
			new_pte->next = as->page_table;
			as->page_table = new_pte;
		}
		return new_pte;

	}
	return NULL;
}


