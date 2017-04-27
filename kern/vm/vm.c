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


struct spinlock cm_spinlock;

unsigned num_total_pages;
unsigned num_free_pages;
struct page_entry *coremap;
unsigned r;

unsigned tlb_index;

void
vm_bootstrap(void)
{
	/* Do nothing. */
	// Not checking for error

	r=0;
	char *disk_name = kstrdup("lhd0raw:");
	int err = vfs_open(disk_name, O_RDWR, 0, &disk);
	if(err){
		kfree(disk_name);
		return;
	}
	struct stat file_stat;

	err = VOP_STAT(disk, &file_stat);
	if(err){
		kfree(disk_name);
		vfs_close(disk);
		return;
	}

	size_t disk_size = file_stat.st_size;
	int nbits = disk_size/PAGE_SIZE;

	swap_table = bitmap_create(nbits);
	if(swap_table == NULL){
		kfree(disk_name);
		vfs_close(disk);
		return;
	}
	kprintf("bitmap slots:%d for disk:%d\n",nbits, disk_size);
	bm_lock = lock_create("bm_lock");
	if(bm_lock == NULL){
		kfree(disk_name);
		vfs_close(disk);
		return;
	}

	biglock = lock_create("biglock");
	if(bm_lock == NULL){
		lock_destroy(bm_lock);
		kfree(disk_name);
		vfs_close(disk);
		return;
	}
	swap_enabled = true;

}

vaddr_t getppages(unsigned npages){

	paddr_t pa=0;
	unsigned i=0,count=0;
	if(swap_enabled != true){

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

		if(count == npages){
			while(count!=0){
				coremap[i].page_state = KERNEL;
				i--;
				count--;
			}
			i++;
			coremap[i].chunk_size = npages;
			pa = i*PAGE_SIZE;
		}
		
		spinlock_release(&cm_spinlock);
		return pa;
	}else{
		spinlock_acquire(&cm_spinlock);
		if(num_free_pages > 0){	
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

			if(count == npages){
				while(count!=0){
					coremap[i].page_state = KERNEL;
					i--;
					count--;
				}
				i++;
				coremap[i].chunk_size = npages;
				pa = i*PAGE_SIZE;
				spinlock_release(&cm_spinlock);
				return pa;
			}
		}

		KASSERT(disk != NULL);
		KASSERT(npages == 1);
		pa = evict_page();
		i = pa/PAGE_SIZE;
		coremap[i].chunk_size = 1;
		struct pte *swap_pte = coremap[i].pte;
		coremap[i].pte = NULL;
		spinlock_release(&cm_spinlock);
		swapout(swap_pte);
		coremap[i].page_state = KERNEL;
		return pa;
	}

	return 0;
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
			num_free_pages--;
		}
	}	
	spinlock_release(&cm_spinlock);
}

vaddr_t alloc_kpages(unsigned npages)
{
	paddr_t pa;
	
	pa = getppages(npages);
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

paddr_t alloc_upage(struct pte *pte){
	
	paddr_t paddr=0;
	unsigned i;

	if(swap_enabled != true){
		spinlock_acquire(&cm_spinlock);
		for(i=0; i<num_total_pages; i++){
			if(coremap[i].page_state == FREE){
				paddr = i*PAGE_SIZE;
				coremap[i].page_state = USER;
				coremap[i].chunk_size = 1;
				coremap[i].pte = pte;
				break;
			}
		}
		spinlock_release(&cm_spinlock);
		return paddr;
	}else{
		KASSERT(disk != NULL);
		spinlock_acquire(&cm_spinlock);
		if(num_free_pages>0){	
			for(i=0; i<num_total_pages; i++){
				if(coremap[i].page_state == FREE){
					paddr = i*PAGE_SIZE;
					coremap[i].page_state = VICTIM;
					coremap[i].chunk_size = 1;
					coremap[i].pte = pte;
					spinlock_release(&cm_spinlock);
					return paddr;
				}
			}
		}

		KASSERT(disk != NULL);
		paddr = evict_page();
		i = paddr/PAGE_SIZE;
		KASSERT(coremap[i].pte != NULL);
		struct pte *swap_pte = coremap[i].pte;
		coremap[i].chunk_size = 1;
		coremap[i].pte = pte;
		spinlock_release(&cm_spinlock);
		swapout(swap_pte);
		return paddr;
	}

	return 0;
}

void free_upage(paddr_t paddr){

	unsigned i = paddr/PAGE_SIZE;

	if(!spinlock_do_i_hold(&cm_spinlock)){
		spinlock_acquire(&cm_spinlock);
	}
	KASSERT(coremap[i].page_state == USER);
	KASSERT(coremap[i].chunk_size == 1);

	num_free_pages--;
	coremap[i].page_state = FREE;
	coremap[i].chunk_size = 0;
	coremap[i].pte = NULL;
	memset((void*)PADDR_TO_KVADDR(paddr), '\0', PAGE_SIZE);

	spinlock_release(&cm_spinlock);
	(void)paddr;
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
 	
 	ehi = faultaddress;
	elo = paddr | TLBLO_VALID | TLBLO_DIRTY | TLBLO_GLOBAL;
	
	i = tlb_probe(faultaddress, 0);
	if(i<0){
		tlb_random(ehi,elo);
	}else{
		tlb_write(ehi,elo, i);
	}

	splx(spl);
	// panic("Ran out of TLB entries - cannot handle page fault\n");
}

static
struct pte* tlb_fault(vaddr_t faultaddress){

	struct pte *page_table = curproc->p_addrspace->page_table;
	vaddr_t vaddr = faultaddress&PAGE_FRAME;
	paddr_t paddr = 0;

	if(swap_enabled != true){
		while(page_table != NULL){
			if(page_table->vaddr == vaddr){
				if(page_table->state == INMEMORY){
					paddr = page_table->paddr;
				}else{
					// ppn = VPN(swapin(faultaddress));
				}
				return page_table;
			}
			page_table = page_table->next;
		}
	}else{
		while(page_table != NULL){
			if(page_table->vaddr == vaddr){
				lock_acquire(page_table->pte_lock);
				if(page_table->state == INMEMORY){
					paddr = page_table->paddr;
					tlb_update(faultaddress, paddr);
				}else{
					KASSERT(swap_enabled == true);
					paddr = swapin(page_table);
					tlb_update(faultaddress, paddr);
					KASSERT(coremap[page_table->paddr/PAGE_SIZE].page_state == VICTIM);
					coremap[page_table->paddr/PAGE_SIZE].page_state = USER;
				}
				lock_release(page_table->pte_lock);
				return page_table;
			}
			page_table = page_table->next;
		}
	}

	struct pte *pte = page_table_add(curproc->p_addrspace , faultaddress);
	if(pte == NULL){
		// kprintf("faultaddress: here%d\n",faultaddress);
		return NULL;
	}
	paddr = pte->paddr;
	if(paddr == 0){
		// kprintf("faultaddress: returning final%d\n",faultaddress);
		return NULL;
	}
	if(swap_enabled == true){
		tlb_update(faultaddress, paddr);
		KASSERT(coremap[pte->paddr/PAGE_SIZE].page_state == VICTIM);
		coremap[pte->paddr/PAGE_SIZE].page_state = USER;
	}
	return pte;
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

// kprintf("Fault addr %p\n",(void*)faultaddress);
if(!valid(faultaddress)){
	// kprintf("Fault addr %p\n",(void*)faultaddress);
	return EFAULT;
}

struct pte *pte;
pte = tlb_fault(faultaddress);
if(pte == NULL){
	// panic("Fault addr %p\n",(void*)faultaddress);
	return ENOMEM;
}
// kprintf("pddr:%p\n", (void*)paddr);

if(swap_enabled != true){
	tlb_update(faultaddress, pte->paddr);
}

return 0;
}
