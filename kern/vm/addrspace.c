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

	struct pte *old = oldas->page_table;

	for(;old!=NULL; old = old->next){

		paddr_t paddr = page_table_add(newas, old->vaddr);
		if(paddr == 0){
			return ENOMEM;
		}
		memmove((void*)PADDR_TO_KVADDR(paddr),
			(const void*)PADDR_TO_KVADDR(old->paddr),
			PAGE_SIZE);
	}

	return 0;
}

// static
// int page_table_copy(struct pte *old, struct pte **ret){

// 	struct pte *new = NULL;
// 	struct pte *prev;

// 	paddr_t paddr;
// 	new = kmalloc(sizeof(struct pte));
// 	if(new == NULL){
// 		return ENOMEM;
// 	}
// 	new->vpn = old->vpn;
// 	new->vaddr = old->vaddr;
// 	paddr = getppages(1, USER, old->vpn*PAGE_SIZE);
// 	if(paddr == 0){
// 		return ENOMEM;
// 	}
// 	if(old->state == INMEMORY){
// 		memmove((void*)PADDR_TO_KVADDR(paddr),
// 		(const void*)PADDR_TO_KVADDR(old->ppn*PAGE_SIZE),
// 		PAGE_SIZE);
// 	}else{
// 		// Copy data from slot in disk to new page allocated
// 	}
// 	new->paddr = paddr;
// 	new->ppn = paddr/PAGE_SIZE;
// 	new->state = old->state;
// 	new->valid = old->valid;
// 	new->referenced = old->referenced;
// 	new->next = NULL;
// 	old = old->next;
// 	prev = new;

// 	while(old != NULL){
// 		// kprintf("copying pte:%p\n",(void*)(old->vpn*PAGE_SIZE));
// 		// kprintf("data:%s\n",(char*)(PADDR_TO_KVADDR(old->ppn*PAGE_SIZE)));
// 		struct pte *curr = kmalloc(sizeof(struct pte));
// 		if(curr == NULL){
// 			return ENOMEM;
// 		}
// 		curr->vpn = old->vpn;
// 		curr->vaddr = old->vaddr;
// 		paddr = getppages(1, USER, old->vpn*PAGE_SIZE);
// 		if(paddr == 0){
// 			return ENOMEM;
// 		}
// 		if(old->state == INMEMORY){
// 			memmove((void*)PADDR_TO_KVADDR(paddr),
// 				(const void*)PADDR_TO_KVADDR(old->ppn*PAGE_SIZE),
// 				PAGE_SIZE);
// 		}else{
// 			// Copy data from slot in disk to new page allocated
// 		}
// 		curr->paddr = paddr;
// 		curr->ppn = paddr/PAGE_SIZE;
// 		curr->state = old->state;
// 		curr->valid = old->valid;
// 		curr->referenced = old->referenced;
// 		curr->next = NULL;
// 		prev->next = curr;
// 		prev = curr;

// 		old = old->next;
// 	}
// 	*ret = new;
// 	return 0;
// }

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
	int readable, writeable, executable;
	while(currseg != NULL){
		vaddr = currseg->vbase;
		memsize = currseg->vend - currseg->vbase;
		readable = currseg->readable;
		writeable = currseg->writeable;
		executable = currseg->executable;

		as_define_region(newas, vaddr, memsize, 
					readable, writeable, executable);

		currseg = currseg->next;
	}

	currseg = old->heap;
	newas->heap->vbase = currseg->vbase;
	newas->heap->vend = currseg->vend;
	newas->heap->readable = currseg->readable;
	newas->heap->writeable = currseg->writeable;
	newas->heap->executable = currseg->executable;

	currseg = old->stack;
	newas->stack->vbase = currseg->vbase;
	newas->stack->vend = currseg->vend;
	newas->stack->readable = currseg->readable;
	newas->stack->writeable = currseg->writeable;
	newas->stack->executable = currseg->executable;

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

	struct pte *page_table = as->page_table;
	struct pte *currpage;
	lock_acquire(as->page_table_lock);
	while(page_table != NULL){
		currpage = page_table;
		page_table = page_table->next;
		if(currpage->state == INMEMORY){
			free_upage(currpage->paddr);
		}else{
			// Clear the slot in the disk
			// bitmap_unmark(swap_table, currpage->ppn);
		}
		kfree(currpage);
	}
	lock_release(as->page_table_lock);
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


/* Add the given page to the page_table*/

paddr_t page_table_add(struct addrspace *as, vaddr_t vaddr){

	struct pte *new_pte = kmalloc(sizeof(struct pte));
	if(new_pte == NULL){
		return 0;
	}
	new_pte->vaddr = vaddr&PAGE_FRAME;
	paddr_t paddr = alloc_upage(new_pte);
	if(paddr == 0){
		return 0;
	}
	new_pte->paddr = paddr;
	new_pte->vpn = VPN(vaddr);
	new_pte->ppn = paddr/PAGE_SIZE;
	new_pte->state = INMEMORY;
	new_pte->next = NULL;

	lock_acquire(as->page_table_lock);
	if(as->page_table == NULL){
		as->page_table = new_pte;
	}else{
		new_pte->next = as->page_table;
		as->page_table = new_pte;
	}
	lock_release(as->page_table_lock);

	return paddr;
}


