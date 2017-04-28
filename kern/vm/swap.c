
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

unsigned r;

paddr_t evict_page(void){

	for(unsigned i = 0; i<num_total_pages; i++){
		r = (r+1)%num_total_pages;
		if(coremap[r].page_state == USER){
			coremap[r].page_state = VICTIM;
			return r*PAGE_SIZE;
		}
	}

	for(unsigned i=0; i<num_total_pages; i++){
		kprintf("%d",coremap[i].page_state );
	}
	panic("no user pages to swapout");
}


void swapout(struct pte *pte){

	paddr_t paddr = pte->paddr;

	lock_acquire(pte->pte_lock);
	int spl;
	spl = splhigh();
	int i = tlb_probe(pte->vaddr, 0);
	if(i>0){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
	if(pte->state == DESTROY){
		lock_release(pte->pte_lock);
		lock_destroy(pte->pte_lock);
		kfree(pte);
		return;
	}
	KASSERT(bitmap_isset(swap_table, pte->disk_slot) > 0);

	write_to_disk(PADDR_TO_KVADDR(paddr), pte->disk_slot);
	pte->state = DISK;
	lock_release(pte->pte_lock);
}

paddr_t swapin(struct pte *pte){

	paddr_t paddr = 0;
	paddr = alloc_upage(pte);
	if(paddr == 0){
		return 0;
	}

	read_from_disk(PADDR_TO_KVADDR(paddr), pte->disk_slot);
	pte->paddr = paddr;
	pte->state = INMEMORY;

	return paddr;
}

int write_to_disk(vaddr_t vaddr, int index){

	struct iovec iov;
	struct uio uio_write;

	uio_kinit(&iov, &uio_write, (void*)vaddr,
		 PAGE_SIZE, index*PAGE_SIZE, UIO_WRITE);

	int err = VOP_WRITE(disk, &uio_write);
	if(err){
		return err;
	}

	return 0;
}

int read_from_disk(vaddr_t vaddr, int index){
	struct iovec iov;
	struct uio uio_read;

	uio_kinit(&iov, &uio_read, (void*)vaddr,
		 PAGE_SIZE, index*PAGE_SIZE, UIO_READ);

	int err = VOP_READ(disk, &uio_read);
	if(err){
		return err;
	}

	return 0;
}
