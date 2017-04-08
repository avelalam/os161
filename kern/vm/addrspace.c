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

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct spinlock cm_spinlock;

unsigned num_total_pages;
struct page_entry *coremap;

static 
vaddr_t getppages(unsigned npages, int page_type){

	paddr_t pa=0;
	unsigned i=0,count=0;

	spinlock_acquire(&cm_spinlock);
	for(i=0; i<num_total_pages; i++){
		if(coremap[i].page_state != FREE){	
			count = 0;
			continue;
		}
		count++;
		if(count == npages){
			break;
		}
	}

	if(count != npages){
		spinlock_release(&cm_spinlock);
		return 0;
	}

	if(count == npages){
		while(count!=0){
			coremap[i].page_state = page_type;
			i--;
			count--;
		}
		i++;
		coremap[i].chunk_size = npages;
		pa = i*PAGE_SIZE;
	}

	spinlock_release(&cm_spinlock);
	return pa;
}


void takeppages(paddr_t paddr, int page_type){

	int i = paddr/4096;

	
	spinlock_acquire(&cm_spinlock);
	if(coremap[i].page_state == page_type){
		int npages = coremap[i].chunk_size;
		coremap[i].chunk_size = 0;
		for(;npages!=0;i++,npages--){
			memset((void*)PADDR_TO_KVADDR(i*PAGE_SIZE), '\0', PAGE_SIZE);	
			coremap[i].page_state = FREE;
		}
	}	
	spinlock_release(&cm_spinlock);
}


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
	// heap->readable = 1;
	// heap->writeable = 1;
	// heap->executable = 0;
	as->heap = heap;

	struct segment *stack = kmalloc(sizeof(struct segment));
	if(stack == NULL){
		return NULL;
	}
	stack->vend = USERSTACK;
	stack->vbase = stack->vend - 1024*PAGE_SIZE;
	// stack->readable = 1;
	// stack->writeable = 1;
	// stack->executable = 0;
	as->stack = stack;

	as->segment_table = NULL;
	as->page_table = NULL;
	/*
	 * Initialize as needed.
	 */
	// kprintf("as created\n");
	return as;
}

static
void page_table_copy(struct pte *old, struct pte **ret){

	struct pte *new = NULL;
	struct pte *prev;

	if(old == NULL){
		*ret = NULL;
		return;
	}
	paddr_t paddr;
	new = kmalloc(sizeof(struct pte));
	new->vpn = old->vpn;
	paddr = getppages(1, USER);
	memmove((void*)PADDR_TO_KVADDR(paddr),
		(const void*)PADDR_TO_KVADDR(old->ppn*PAGE_SIZE),
		PAGE_SIZE);
	new->ppn = paddr/PAGE_SIZE;
	new->state = old->state;
	new->valid = old->valid;
	new->referenced = old->referenced;
	new->next = NULL;
	old = old->next;
	prev = new;

	while(old != NULL){
		// kprintf("copying pte:%p\n",(void*)(old->vpn*PAGE_SIZE));
		// kprintf("data:%s\n",(char*)(PADDR_TO_KVADDR(old->ppn*PAGE_SIZE)));
		struct pte *curr = kmalloc(sizeof(struct pte));
		curr->vpn = old->vpn;
		paddr = getppages(1, USER);
		memmove((void*)PADDR_TO_KVADDR(paddr),
			(const void*)PADDR_TO_KVADDR(old->ppn*PAGE_SIZE),
			PAGE_SIZE);
		curr->ppn = paddr/PAGE_SIZE;
		curr->state = old->state;
		curr->valid = old->valid;
		curr->referenced = old->referenced;
		curr->next = NULL;
		prev->next = curr;
		prev = curr;

		old = old->next;
	}
	*ret = new;
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
	if(currseg == NULL){
		page_table_copy(old->page_table, &newas->page_table);
		*ret = newas;
		return 0;
	}

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

	page_table_copy(old->page_table, &newas->page_table);



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
	while(page_table != NULL){
		currpage = page_table;
		page_table = page_table->next;
		takeppages((currpage->ppn)*PAGE_SIZE, USER);
		kfree(currpage);
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
	// new_segment->readable = readable;
	// new_segment->writeable = writeable;
	// new_segment->executable = executable;
	
	struct segment *curr = as->segment_table;
	while(curr != NULL && curr->next != NULL){
		curr = curr->next;
	}
	if(curr == NULL){
		as->segment_table = new_segment;
	}else{
		curr->next = new_segment;
	}
	// as->heap->vbase =  new_segment->vend;
	as->heap->vend = new_segment->vend + PAGE_SIZE - (new_segment->vend % PAGE_SIZE);
	as->heap->vbase = as->heap->vend;
	// kprintf("loaded segment from %p to %p\n",(void*)new_segment->vbase, (void*)new_segment->vend );

	curr = as->segment_table;
	while(curr!=NULL){
		// kprintf("%p,%p\n",(void*)curr->vbase, (void*)curr->vend);
		curr = curr->next;
	}
	// kprintf("heap:%p,%p\n", (void*)as->heap->vbase, (void*)as->heap->vend);

	// returning 0,,,check with this
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
static 
void page_table_add(paddr_t paddr, vaddr_t vaddr){

	struct pte *new_pte = kmalloc(sizeof(struct pte));
	
	new_pte->vpn = VPN(vaddr);
	new_pte->ppn = paddr/PAGE_SIZE;
	new_pte->next = NULL;

	struct pte *page_table = curproc->p_addrspace->page_table;

	if(page_table == NULL){
		curproc->p_addrspace->page_table = new_pte;
	}else{
		new_pte->next = page_table;
		curproc->p_addrspace->page_table = new_pte;
	}
	// kprintf("paddr:%p\n",(void*)paddr);
	// kprintf("added pte vpn:%d, ppn:%d, vaddr:%p\n", new_pte->vpn, new_pte->ppn, (void*)vaddr);
}

void
vm_bootstrap(void)
{
	/* Do nothing. */
}

vaddr_t alloc_kpages(unsigned npages)
{
	paddr_t pa;
	
	pa = getppages(npages, KERNEL);
	if(pa == 0){
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
	
}

void
free_kpages(vaddr_t vaddr)
{
	paddr_t pa = KVADDR_TO_PADDR(vaddr);
	KASSERT(pa%PAGE_SIZE==0);
	
	takeppages(pa, KERNEL);
	(void)vaddr;
}

unsigned
int
coremap_used_bytes() {

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */

	unsigned count = 0,i;
	for(i=0; i<num_total_pages; i++){
		if(coremap[i].page_state!=FREE){
			count++;
		}
	}
	
	return count*PAGE_SIZE;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

static
void tlb_update(vaddr_t faultaddress, paddr_t paddr){

	uint32_t ehi, elo;
	int spl;
	int i;


	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	i = tlb_probe(faultaddress, 0);
	ehi = faultaddress;
	elo = paddr | TLBLO_VALID | TLBLO_DIRTY | TLBLO_GLOBAL;
	if(i < 0){
		tlb_random(ehi, elo);
	}else{
		tlb_write(ehi, elo, i);
	}
	
	splx(spl);
	// panic("Ran out of TLB entries - cannot handle page fault\n");
}

static
paddr_t tlb_fault(vaddr_t faultaddress){

	struct pte *page_table = curproc->p_addrspace->page_table;
	int vpn = VPN(faultaddress);
	int ppn;
	paddr_t paddr;
	// kprintf("searching vpn:%d\n",vpn);
	while(page_table != NULL){
		// kprintf("curr vpn:%d\n",page_table->vpn);
		if(page_table->vpn == vpn){
			ppn = page_table->ppn;
			paddr = ppn*PAGE_SIZE;
			return paddr;
		}
		page_table = page_table->next;
	}

	paddr = getppages(1, USER);

	page_table_add(paddr, faultaddress);

	return paddr;
}

static 
int valid(vaddr_t faultaddress){

	struct addrspace *as = curproc->p_addrspace;
	struct segment *segment = as->segment_table;
	// kprintf("given:%p\n",(void*)faultaddress);
	while(segment!= NULL){
		// kprintf("checking vbase:%p, vend:%p\n", (void*)segment->vbase, (void*)segment->vend);
		if(segment->vbase <= faultaddress && faultaddress < segment->vend ){
			return 1;
		}
		segment = segment->next;
	}

	// kprintf("Fault addr %p\n",(void*)faultaddress);
	segment = as->heap;
	if(segment->vbase <= faultaddress && faultaddress < segment->vend ){
			return 1;
	}

	segment = as->stack;
	if(segment->vbase <= faultaddress && faultaddress < segment->vend ){
			return 1;
	}

	return 0;
}



int
vm_fault(int faulttype, vaddr_t faultaddress)
{

(void)faulttype;
(void)faultaddress;

if(!valid(faultaddress)){
	kprintf("Fault addr %p\n",(void*)faultaddress);
	return EFAULT;
}

paddr_t paddr;

paddr = tlb_fault(faultaddress);
// kprintf("pddr:%p\n", (void*)paddr);

/* make sure it's page-aligned */
KASSERT((paddr & PAGE_FRAME) == paddr);

tlb_update(faultaddress, paddr);



return 0;
}
