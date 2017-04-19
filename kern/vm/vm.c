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


struct spinlock cm_spinlock;

unsigned num_total_pages;
struct page_entry *coremap;


void
vm_bootstrap(void)
{
	/* Do nothing. */
	// Not checking for error

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

	bm_lock = lock_create("bm_lock");
	if(bm_lock == NULL){
		kfree(disk_name);
		vfs_close(disk);
		return;
	}

	swap_enabled = true;


}





vaddr_t getppages(unsigned npages, int page_type, vaddr_t vaddr){

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
				coremap[i].page_state = page_type;
				i--;
				count--;
			}
			i++;
			coremap[i].chunk_size = npages;
			pa = i*PAGE_SIZE;
			if(page_type == USER){
				// coremap[i].as = proc_getas();
				coremap[i].vaddr = vaddr;
			}
		}
		
		spinlock_release(&cm_spinlock);
		return pa;
	}else{

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
		}
	}	
	spinlock_release(&cm_spinlock);
}

vaddr_t alloc_kpages(unsigned npages)
{
	paddr_t pa;
	
	pa = getppages(npages, KERNEL, 0);
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

	}

	return 0;
}

void free_upage(paddr_t paddr){

	unsigned i = paddr/PAGE_SIZE;

	spinlock_acquire(&cm_spinlock);
	KASSERT(coremap[i].page_state == USER);
	KASSERT(coremap[i].chunk_size == 1);

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
	vaddr_t vaddr = faultaddress&PAGE_FRAME;
	paddr_t paddr = 0;

	if(swap_enabled != true){
		lock_acquire(curproc->p_addrspace->page_table_lock);
		while(page_table != NULL){
			if(page_table->vaddr == vaddr){
				if(page_table->state == INMEMORY){
					paddr = page_table->paddr;
				}else{
					// ppn = VPN(swapin(faultaddress));
				}
				lock_release(curproc->p_addrspace->page_table_lock);
				return paddr;
			}
			page_table = page_table->next;
		}
		lock_release(curproc->p_addrspace->page_table_lock);
	}else{
		
	}

	struct pte *pte = page_table_add(curproc->p_addrspace , faultaddress);
	if(pte == NULL){
		return ENOMEM;
	}
	paddr = pte->paddr;

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

// kprintf("Fault addr %p\n",(void*)faultaddress);
if(!valid(faultaddress)){
	// kprintf("Fault addr %p\n",(void*)faultaddress);
	return EFAULT;
}

paddr_t paddr;

paddr = tlb_fault(faultaddress);
if(paddr == 0){
	return ENOMEM;
}
// kprintf("pddr:%p\n", (void*)paddr);

/* make sure it's page-aligned */
KASSERT((paddr & PAGE_FRAME) == paddr);

tlb_update(faultaddress, paddr);



return 0;
}
