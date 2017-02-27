
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

int sys_write(int fd, void* buf,int buflen,int32_t* retval){
	
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
                *retval=-1;
		return err;
	}
	
	uio_write.uio_rw = UIO_WRITE;
	uio_write.uio_space = proc_getas();
	uio_write.uio_segflg = UIO_USERSPACE;
	uio_write.uio_iovcnt = 1;
	uio_write.uio_iov->iov_ubase = (userptr_t)buf;
	uio_write.uio_iov->iov_len = len;
	uio_write.uio_resid = len;
		
	
	err = VOP_WRITE((curproc->file_table[fd]->fileobj), &uio_write);		
        if(err){
          *retval=-1;
          return err;
        }
	curproc->file_table[fd]->offset +=len-uio_write.uio_resid;
       *retval=len-uio_write.uio_resid;
       	return 0; 
}


int sys_open(char *filename,int flags,int *retval){
          
      struct fh *file_handle;
   
       file_handle=kmalloc(sizeof(struct fh));
       
     //  file_handle->offset= 
         file_handle->mode=flags;
         file_handle->num_refs=1;
         file_handle->fh_lock=lock_create("sdjf");
   
        int fd=curproc->next_fd;        


      int err=vfs_open(filename,flags,0,&(curproc->file_table[fd]->fileobj));
        if(err)return err;
 
      *retval=fd;
       curproc->next_fd++;
          return 0;  
}


