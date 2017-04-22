
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

paddr_t evict_page(void){

	int r;
	struct timespec time_;

	gettime(&time_);
	r = time_.tv_nsec % num_total_pages;
	for(unsigned i = 0; i<num_total_pages; i++){
		if(coremap[r].page_state == USER){
			// coremap[r].page_state = VICTIM;
			return r*PAGE_SIZE;
		}
		r = (r+1)%num_total_pages;
	}

	for(unsigned i=0; i<num_total_pages; i++){
		kprintf("%d",coremap[i].page_state );
	}
	panic("no user pages to swapout");

	// while(coremap[r].page_state != USER){
	// 	gettime(&time_);
	// 	r = time_.tv_nsec % num_total_pages;
	// }
	// coremap[r].page_state = DIRTY;
	// return r*PAGE_SIZE;
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

	// int err = bitmap_alloc(swap_table, &pte->disk_slot);
	// if(err){
	// 	panic("error in swapout:bitmap\n");
	// }
	KASSERT(bitmap_isset(swap_table, pte->disk_slot) > 0);
	// KASSERT(coremap[pte->paddr/PAGE_SIZE].page_state == VICTIM);
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
	// bitmap_unmark(swap_table, pte->disk_slot);
	pte->paddr = paddr;
	// pte->ppn = paddr/PAGE_SIZE;
	pte->state = INMEMORY;

	return paddr;
}

int write_to_disk(vaddr_t vaddr, int index){

	struct iovec iov;
	struct uio uio_write;

	// iov.iov_kbase = (void*)vaddr;
	// iov.iov_len = PAGE_SIZE;

	// uio_write.uio_iov = &iov;
	// uio_write.uio_iovcnt = 1;
	// uio_write.uio_offset = index*PAGE_SIZE;
	// uio_write.uio_resid = PAGE_SIZE;
	// uio_write.uio_segflg = UIO_SYSSPACE;
	// uio_write.uio_rw = UIO_WRITE;
	// uio_write.uio_space = NULL;

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

	// iov.iov_kbase = (void*)vaddr;
	// iov.iov_len = PAGE_SIZE;

	// uio_read.uio_iov = &iov;
	// uio_read.uio_iovcnt = 1;
	// uio_read.uio_offset = index*PAGE_SIZE;
	// uio_read.uio_resid = PAGE_SIZE;
	// uio_read.uio_segflg = UIO_SYSSPACE;
	// uio_read.uio_rw = UIO_READ;
	// uio_read.uio_space = NULL;

	uio_kinit(&iov, &uio_read, (void*)vaddr,
		 PAGE_SIZE, index*PAGE_SIZE, UIO_READ);

	int err = VOP_READ(disk, &uio_read);
	if(err){
		return err;
	}

	return 0;
}
