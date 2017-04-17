
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

static
paddr_t evict_page(){

	int r;
	struct timespec time_;

	gettime(&time_);
	r = time_.tv_nsec % num_total_pages;
	while(coremap[r].page_state == KERNEL){
		gettime(&time_);
		r = time_.tv_nsec % num_total_pages;
	}
	return r*PAGE_SIZE;
}


paddr_t swapout(void){
	paddr_t paddr = 0;

	paddr = evict_page();

	unsigned index = paddr/PAGE_SIZE;

	struct pte *page_table = coremap[index].as->page_table;
	if(page_table == NULL){
		return 0;
	}

	unsigned disk_slot;
	int err = bitmap_alloc(swap_table, &disk_slot);
	if(err){
		return 0;
	}
	

	if(curproc->p_addrspace != coremap[index].as){
		lock_acquire(coremap[index].as->page_table_lock);
	}
	err = write_to_disk(PADDR_TO_KVADDR(paddr), disk_slot);
	if(err){
		if(curproc->p_addrspace != coremap[index].as){
			lock_release(coremap[index].as->page_table_lock);
		}
		return 0;
	}
	while((unsigned)page_table->paddr != paddr){
		page_table = page_table->next;
	}
	page_table->state = DISK;
	page_table->paddr = disk_slot;
	if(curproc->p_addrspace != coremap[index].as){
		lock_release(coremap[index].as->page_table_lock);
	}
	return paddr;
}

paddr_t swapin(vaddr_t faultaddress){

	paddr_t paddr = 0;
	paddr = getppages(1, USER, faultaddress);

	if(paddr == 0){
		return 0;
	}

	struct pte *page_table = curproc->p_addrspace->page_table;
	while((unsigned)page_table->vaddr!=(faultaddress&PAGE_FRAME)){
		page_table = page_table->next;
	}
	int disk_slot = page_table->paddr;
	read_from_disk(PADDR_TO_KVADDR(paddr), disk_slot);
	bitmap_unmark(swap_table, disk_slot);
	page_table->paddr = paddr/PAGE_SIZE;
	page_table->state = INMEMORY;

	return paddr;
}

int write_to_disk(vaddr_t vaddr, int index){

	struct iovec iov;
	struct uio uio_write;

	iov.iov_kbase = (void*)vaddr;
	iov.iov_len = PAGE_SIZE;

	uio_write.uio_iov = &iov;
	uio_write.uio_iovcnt = 1;
	uio_write.uio_offset = index*PAGE_SIZE;
	uio_write.uio_resid = PAGE_SIZE;
	uio_write.uio_segflg = UIO_SYSSPACE;
	uio_write.uio_rw = UIO_WRITE;
	uio_write.uio_space = NULL;

	int err = VOP_WRITE(disk, &uio_write);
	if(err){
		return err;
	}

	return 0;
}

int read_from_disk(vaddr_t vaddr, int index){
	struct iovec iov;
	struct uio uio_read;

	iov.iov_kbase = (void*)vaddr;
	iov.iov_len = PAGE_SIZE;

	uio_read.uio_iov = &iov;
	uio_read.uio_iovcnt = 1;
	uio_read.uio_offset = index*PAGE_SIZE;
	uio_read.uio_resid = PAGE_SIZE;
	uio_read.uio_segflg = UIO_SYSSPACE;
	uio_read.uio_rw = UIO_READ;
	uio_read.uio_space = NULL;

	int err = VOP_READ(disk, &uio_read);
	if(err){
		return err;
	}

	return 0;
}
