
#include <types.h>
#include <uio.h>
#include <copyinout.h>
#include <file_syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <vnode.h>
#include <proc.h>

int sys_write(int fd, userptr_t buf,int buflen){
	
	if(fd<0 || fd>63){
		return EBADF;
	}
	if((curproc)->file_table[fd]==NULL){
		return EBADF;
	}
	struct uio uio_write;
	int len = (int) buflen, err=0;
	char data[256];
	err = copyin(buf, data, len);
	if(err){
		return err;
	}
	
	uio_write.uio_rw = UIO_WRITE;
	uio_write.uio_space = proc_getas();
	uio_write.uio_segflg = UIO_USERSPACE;
	uio_write.uio_iovcnt = 1;
	uio_write.uio_iov->iov_ubase = buf;
	uio_write.uio_iov->iov_len = len;
	uio_write.uio_resid = len;
		
	
	err = VOP_WRITE((curproc->file_table[fd]->fileobj), &uio_write);		
	return err;
}
