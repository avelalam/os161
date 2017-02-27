
#include <types.h>
#include <uio.h>
#include <copyinout.h>
#include <file_syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <vnode.h>
#include <proc.h>
#include <synch.h>
#include<vfs.h>
#include <kern/fcntl.h>

int sys_write(int fd, void* buf,int buflen){
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
	uio_write.uio_iov->iov_ubase = (userptr_t)buf;
	uio_write.uio_iov->iov_len = len;
	uio_write.uio_resid = len;
	uio_write.uio_offset = curproc->file_table[fd]->offset;		
	
	err = VOP_WRITE((curproc->file_table[fd]->fileobj), &uio_write);		
        if(err){
		return err;
        }
	curproc->file_table[fd]->offset = uio_write.uio_offset;
       	return -(len-uio_write.uio_resid); 
}


int sys_open(char *filename,int flags){

	struct fh *file_handle;
	struct vnode *fileobj;
	int fd,err;
	fd = curproc->next_fd;	
	file_handle=kmalloc(sizeof(struct fh));

	file_handle->offset=0; 
	file_handle->mode=flags;
	file_handle->num_refs=1;
	file_handle->fh_lock=lock_create("sdjf");
	
	
	err=vfs_open(filename,flags,0,&(fileobj)); 
	if(err){
		return err;
	}
	if(fileobj == NULL && ((flags & O_CREAT) == O_CREAT)){
		err = VOP_CREAT(curproc->p_cwd, filename, ((flags&O_EXCL)==O_EXCL), 0, &(fileobj));
		if(err){
			return err;
		}
	}
	if((flags&O_TRUNC) == O_TRUNC){
		err = VOP_TRUNCATE(fileobj, 0);
		if(err){
			return err;
		}
	}
	

	file_handle->fileobj = fileobj;
	curproc->file_table[fd] = file_handle;
	curproc->next_fd++;
	kprintf("opened\n");
	return -fd;  
}

int sys_read(int fd, void* buf, int buflen){

	if(fd<0 ||fd >63){
		return EBADF;
	}
	if((curproc)->file_table[fd]==NULL){
		return EBADF;
	}

	struct uio uio_read;
	int len = (int) buflen, err=0;	
	char data[256];
	err = copyin(buf,data,len);
	if(err){
		return err;
	}
	uio_read.uio_rw = UIO_READ;
	uio_read.uio_space = proc_getas();
	uio_read.uio_segflg = UIO_USERSPACE;
	uio_read.uio_iovcnt = 1;
	uio_read.uio_iov->iov_ubase = (userptr_t)buf;
	uio_read.uio_iov->iov_len = len;
	uio_read.uio_resid = len;
	uio_read.uio_offset = curproc->file_table[fd]->offset;

	kprintf("reading\n");
	err = VOP_READ((curproc->file_table[fd]->fileobj),&uio_read);
	kprintf("read\n");
	if(err){
		return err;	
	}
	
	curproc->file_table[fd]->offset = uio_read.uio_offset;
       	return -(len-uio_read.uio_resid); 

	(void)fd;
	(void)buf;
	(void)buflen;
	return 0;
}
