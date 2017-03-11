
#include <types.h>
#include <uio.h>
#include <copyinout.h>
#include <file_syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <vnode.h>
#include <proc.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <syscall.h>
#include <limits.h>
#define EOF (-1)

int sys_write(int fd, const void *buf,int buflen){
	if(fd<0 || fd>63){
		return EBADF;
	}
	if((curproc)->file_table[fd]==NULL){
		return EBADF;
	}
	lock_acquire(curproc->file_table[fd]->fh_lock);
	
	struct uio uio_write;
	struct iovec iov;
	int len = (int) buflen, err=0;
	
	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = len;
	
	uio_write.uio_iov = &iov;
	uio_write.uio_rw = UIO_WRITE;
	uio_write.uio_space = proc_getas();
	uio_write.uio_segflg = UIO_USERSPACE;
	uio_write.uio_iovcnt = 1;
	uio_write.uio_resid = len;
	uio_write.uio_offset = curproc->file_table[fd]->offset;		
	
	//kprintf("writing\n");	
	err = VOP_WRITE((curproc->file_table[fd]->fileobj), &uio_write);		
	//kprintf("done writing\n");
	if(err){
		lock_release(curproc->file_table[fd]->fh_lock);
		return err;
        }
	curproc->file_table[fd]->offset = uio_write.uio_offset;
	lock_release(curproc->file_table[fd]->fh_lock);
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

	lock_acquire(curproc->file_table[fd]->fh_lock);
	struct uio uio_read;
	struct iovec iov;
	int len = (int) buflen, err=0;	
	
	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen;

	uio_read.uio_iov = &iov;
	uio_read.uio_rw = UIO_READ;
	uio_read.uio_space = proc_getas();
	uio_read.uio_segflg = UIO_USERSPACE;
	uio_read.uio_iovcnt = 1;
	uio_read.uio_resid = len;
	uio_read.uio_offset = curproc->file_table[fd]->offset;

	err = VOP_READ((curproc->file_table[fd]->fileobj),&uio_read);
	if(err){
		lock_release(curproc->file_table[fd]->fh_lock);
		return err;	
	}
	
	curproc->file_table[fd]->offset = uio_read.uio_offset;
	lock_release(curproc->file_table[fd]->fh_lock);
       	return -(len-uio_read.uio_resid); 
}

int sys_close(int fd){
	if(fd>63 || fd <0){
		return EBADF;
	}
	if(curproc->file_table[fd] == NULL){
		return EBADF;
	}
	lock_acquire(curproc->file_table[fd]->fh_lock);	
	if((curproc->file_table[fd]->num_refs == 1)){
		vfs_close(curproc->file_table[fd]->fileobj);
		lock_release(curproc->file_table[fd]->fh_lock);
		kfree(curproc->file_table[fd]);
		curproc->file_table[fd] = NULL;	
	}else{
		curproc->file_table[fd]->num_refs--;
		lock_release(curproc->file_table[fd]->fh_lock);
	}
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
off_t sys_lseek(int fd,int high_32,int low_32,const void *whence_ptr){

 	if(fd<0 || fd>63){
		return EBADF;
	}
	if(curproc->file_table[fd] == NULL){
		return EBADF;
	}
	int whence,err;
	err = copyin((userptr_t)whence_ptr, &whence, sizeof(int));
	if(err){
		return err;
	}
	bool b;	
	lock_acquire(curproc->file_table[fd]->fh_lock);	
	b = VOP_ISSEEKABLE(curproc->file_table[fd]->fileobj);
	if(b == false){
		return ESPIPE;
	}
	struct stat file_stat;
	
	err = VOP_STAT(curproc->file_table[fd]->fileobj, &file_stat);
	if(err){
		return err;
	}
	off_t cursize = file_stat.st_size;
	
	off_t pos=0;
	pos=pos|high_32;
	pos<<=32;
	pos=pos|low_32;
	if(whence==SEEK_SET){
		curproc->file_table[fd]->offset=pos;
		lock_release(curproc->file_table[fd]->fh_lock);
		return -(curproc->file_table[fd]->offset);
	} 
	
	if(whence==SEEK_CUR){
		curproc->file_table[fd]->offset+=pos;
		lock_release(curproc->file_table[fd]->fh_lock);
		return -(curproc->file_table[fd]->offset);
	}

	if(whence==SEEK_END){
		curproc->file_table[fd]->offset=cursize+pos;
		lock_release(curproc->file_table[fd]->fh_lock);
		return -(curproc->file_table[fd]->offset);
	}

	lock_release(curproc->file_table[fd]->fh_lock);
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


int sys_fork(struct trapframe *tf){

	struct proc *newproc;
	struct trapframe *child_tf;
	
	child_tf = kmalloc(sizeof(struct trapframe));
	if(child_tf == NULL){
		return ENOMEM;
	}
	*child_tf = *tf;
	
	newproc = proc_create_runprogram("child");
	if(newproc == NULL){
		return ENOMEM;
	}

	int err = as_copy(curproc->p_addrspace, &newproc->p_addrspace);
	if(err){
		return err;
	}	
	
	newproc->next_fd = curproc->next_fd;
	for(int i=0; i<64; i++){
		if(curproc->file_table[i] == NULL){
			newproc->file_table[i] = NULL;
		}else{	
			lock_acquire(curproc->file_table[i]->fh_lock);	
			curproc->file_table[i]->num_refs++;
			newproc->file_table[i] = curproc->file_table[i];
			lock_release(curproc->file_table[i]->fh_lock);
		}
	}
	newproc->ppid = curproc->pid;
	newproc->exit_status = false;
	newproc->proc_sem = sem_create("procsec",0);

	lock_acquire(process_table->pt_lock);	
	newproc->pid = process_table->next_pid++;
	process_table->proc_table[newproc->pid] = newproc;	
	lock_release(process_table->pt_lock);	

	err = thread_fork("childthread", newproc, enter_forked_process, (void*)child_tf, 0);
	if(err){
		return err;
	}

	return -(newproc->pid);
}

int sys_getpid(){
	return -(curproc->pid);
}

void sys__exit(int exitcode){

	int code = _MKWAIT_EXIT(exitcode);
	
	lock_acquire(process_table->pt_lock);
	process_table->proc_table[curproc->pid]->exit_status = true;
	process_table->proc_table[curproc->pid]->exitcode = code;
	lock_release(process_table->pt_lock);
	
	V(curproc->proc_sem);
	thread_exit();

}

int sys_waitpid(int pid, void* status, int options){
	if(options != 0){
		return EINVAL;
	}
	if(pid < 3 || pid > 32){
		return ECHILD;
	}
	struct proc *child_proc;
	int err;
	child_proc = process_table->proc_table[pid];
	if(child_proc==NULL){
		return ESRCH;
	}
	P(child_proc->proc_sem);
	if(status != NULL){
		err = copyout(&child_proc->exitcode, (userptr_t) status, sizeof(int ));
		if(err){
			proc_destroy(child_proc);
			process_table->proc_table[pid] = NULL;
			return err;
		}
	}	
	proc_destroy(child_proc);
	process_table->proc_table[pid] = NULL;
	return -pid;
}

int sys_execv(char *prog_name,char **args){
	
	int err, argc=0;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;		
	char *name;
	char **inargs;
	size_t len, str_len;
	name = kmalloc(strlen(prog_name));
	err = copyinstr((const_userptr_t)prog_name, name, PATH_MAX, &len);
	if(err){
		return err;
	}
	args++;
	argc++;
	kprintf("here\n");

	int index = buffer->curindex;
	while(1){
	
		inargs = kmalloc(sizeof(char**));
		err = copyin((const_userptr_t)args, inargs, sizeof(int));	
		if(err){
			return err;
		}	
		if(*inargs == NULL){
			kfree(inargs);
			break;
		}
		str_len = strlen(*inargs);
		
		err = copyin((const_userptr_t)(*args), &buffer->buffer[buffer->curindex], str_len);
		if(err){
			return err;
		}
		buffer->curindex += str_len;
		buffer->buffer[buffer->curindex++] = '';
		kprintf("buf:%s\n", buffer->buffer);
		kfree(inargs);
		args++;
	}

	err = vfs_open(prog_name, O_RDONLY, 0, &v);
	if(err){
		return err;
	}

	curproc->p_addrspace = as_create();
	proc_setas(curproc->p_addrspace);
	as_activate();
		
	err = load_elf(v, &entrypoint);
	if(err){
		vfs_close(v);
		return err;
	}
	vfs_close(v);

	err = as_define_stack(curproc->p_addrspace, &stackptr);
	if(err){
		return err;
	}
	
	enter_new_process(argc, (userptr_t)args, NULL, stackptr, entrypoint);
//	panic("should not return here\n");
	(void)inargs;
	(void)prog_name;
	(void)args;
	
	return 0;
}
