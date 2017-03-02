
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
#include<kern/seek.h>
#define EOF (-1)

int sys_write(int fd, const void *buf,int buflen){
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
	//kprintf("writing\n");	
	err = VOP_WRITE((curproc->file_table[fd]->fileobj), &uio_write);		
        //kprintf("done writing\n");
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

int sys_close(int fd){
	kprintf("closing\n");
	if(fd>63 || fd <0){
		return EBADF;
	}	
	if((curproc->file_table[fd]->num_refs == 1)){
		vfs_close(curproc->file_table[fd]->fileobj);
		kfree(curproc->file_table[fd]);
		curproc->file_table[fd] = NULL;	
	}else{
		curproc->file_table[fd]->num_refs--;
	}
	(void)fd;
	kprintf("closed\n");
	return 0;
}

int sys_chdir(const void *pathname){

	int err;
	
	err = vfs_chdir((char *)pathname);
	if(err){
		return err;
	}
	return 0;
}
off_t sys_lseek(int fd,int low_32,int high_32,int whence){

  if(fd<0 || fd>63)return EBADF;
  bool b=VOP_ISSEEKABLE(curproc->file_table[fd]->fileobj);
  if(b==false)return ESPIPE;
  if(whence!=SEEK_SET || whence!=SEEK_CUR || whence!=SEEK_END)return EINVAL;
 
  long long pos=0;
   pos=pos|high_32;
   pos<<=32;
   pos=pos|low_32; 
  
   if(whence==SEEK_SET){
     curproc->file_table[fd]->offset=pos;
  return -(curproc->file_table[fd]->offset);
   } 
  if(whence==SEEK_CUR){
       curproc->file_table[fd]->offset+=pos;
  return -(curproc->file_table[fd]->offset);
  }
  if(whence==SEEK_END){
      curproc->file_table[fd]->offset=EOF+pos;
  return -(curproc->file_table[fd]->offset);
  }
 
  return EINVAL;
 return 0;
 
}

int sys_dup2(int oldfd,int newfd){

 if(oldfd<0   || oldfd>63 || newfd<0 || newfd>63)return EBADF;

  
   if(curproc->file_table[newfd]->fileobj!=NULL){
        vfs_close(curproc->file_table[newfd]->fileobj);
    }
    curproc->file_table[newfd]=curproc->file_table[oldfd];

    return -(newfd);

}

int sys_getcwd(char *buf,size_t buflen){
 
  
      struct uio uio_getcwd;    
      uio_getcwd.uio_iovcnt=1;
      uio_getcwd.uio_offset=0;
      uio_getcwd.uio_resid=buflen;
      uio_getcwd.uio_rw=UIO_READ;
      uio_getcwd.uio_space=proc_getas();
      uio_getcwd.uio_iov->iov_kbase = (char *)buf;
      uio_getcwd.uio_iov->iov_len = buflen;
    int err=vfs_getcwd(&uio_getcwd);
     if(err){
           return err;
     }
   
    return -(buflen-uio_getcwd.uio_resid);
   
 
}





