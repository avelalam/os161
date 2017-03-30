
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
	
	if(!(curproc->file_table[fd]->mode == O_WRONLY || curproc->file_table[fd]->mode == O_RDWR)){
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
	size_t len;
	char name[PATH_MAX];

	err = copyinstr((userptr_t)filename, name, PATH_MAX, &len);
	if(err){
		return err;
	}
	fd = curproc->next_fd;
	file_handle=kmalloc(sizeof(struct fh));
	file_handle->offset=0; 
	file_handle->mode=flags & O_ACCMODE;
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

	if(!(curproc->file_table[fd]->mode == O_RDONLY || curproc->file_table[fd]->mode == O_RDWR)){
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
		kfree(curproc->file_table[fd]->fh_lock);
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
	char name[PATH_MAX];
	size_t len;
	
	err = copyinstr((userptr_t)pathname, name, PATH_MAX, &len);
	if(err){
		return err;
	}	
	err = vfs_chdir((char *)pathname);
	if(err){
		return err;
	}
	return 0;
}
off_t sys_lseek(int fd,int high_32,int low_32,const void *whence_ptr){

 	if(fd<3 || fd>63){
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
		if(pos<0){
			lock_release(curproc->file_table[fd]->fh_lock);
			return EINVAL;
		}
		curproc->file_table[fd]->offset=pos;
		lock_release(curproc->file_table[fd]->fh_lock);
		return -(curproc->file_table[fd]->offset);
	} 
	
	if(whence==SEEK_CUR){
		if(curproc->file_table[fd]->offset+pos<0){
			lock_release(curproc->file_table[fd]->fh_lock);
			return EINVAL;
		}
		curproc->file_table[fd]->offset+=pos;
		lock_release(curproc->file_table[fd]->fh_lock);
		return -(curproc->file_table[fd]->offset);
	}

	if(whence==SEEK_END){
		if(cursize+pos < 0){
			lock_release(curproc->file_table[fd]->fh_lock);
			return EINVAL;
		}
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
	
	if(curproc->file_table[oldfd]==NULL){
		return EBADF;
	}	
	
 	lock_acquire(curproc->file_table[oldfd]->fh_lock);
	if(curproc->file_table[newfd]!=NULL){
		vfs_close(curproc->file_table[newfd]->fileobj);
	}
	curproc->file_table[newfd]=curproc->file_table[oldfd];
	curproc->file_table[oldfd]->num_refs++;
	lock_release(curproc->file_table[oldfd]->fh_lock);
	

	
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
	int pid;
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

	lock_acquire(pt_lock);	
	for(pid=3; pid<200; pid++){
		if(proc_table[pid] == NULL){
			break;
		}
	}
	newproc->pid = pid;
	proc_table[newproc->pid] = newproc;	
	lock_release(pt_lock);	

	err = thread_fork("childthread", newproc, enter_forked_process, (void*)child_tf, 0);
	if(err){
		return err;
	}

	P(newproc->proc_sem);
	kfree(child_tf);	
	return -(newproc->pid);
}

int sys_getpid(){
	return -(curproc->pid);
}

void sys__exit(int exitcode){

	int code=0;
	code = _MKWAIT_EXIT(exitcode);
	lock_acquire(pt_lock);
	proc_table[curproc->pid]->exit_status = true;
	proc_table[curproc->pid]->exitcode = code;
	lock_release(pt_lock);
	
	for(int i=0; i<64; i++){
                if(curproc->file_table[i] != NULL){
			if(curproc->file_table[i]->num_refs == 1){                                 
				vfs_close(curproc->file_table[i]->fileobj);
				kfree(curproc->file_table[i]->fh_lock);
				kfree(curproc->file_table[i]);                                     
                       		curproc->file_table[i] = NULL;
			 }else{  
                                curproc->file_table[i]->num_refs--;                                
                        }                                                                             
                }                                                                                     
	}
	V(curproc->proc_sem);
	thread_exit();

}

int sys_waitpid(int pid, void* status, int options){
	if(options != 0){
		return EINVAL;
	}
	if(pid < 2 || pid > 200){
		return ECHILD;
	}
	if(pid == curproc->pid || pid == curproc->ppid){
		return ECHILD;
	}
	struct proc *child_proc;
	int err;
	child_proc = proc_table[pid];
	if(child_proc==NULL){
		return ESRCH;
	}
	if(curproc->pid!=0 && child_proc->ppid != curproc->pid){
		return ECHILD;
	}
	kprintf("here\n");
	
	P(child_proc->proc_sem);
	if(status != NULL){
		err = copyout(&child_proc->exitcode, (userptr_t) status, sizeof(int ));
		if(err){
			sem_destroy(child_proc->proc_sem);	
			proc_destroy(child_proc);
			proc_table[pid] = NULL;
			return err;
		}
	}
	kprintf("destroying %d\n",pid);
	sem_destroy(child_proc->proc_sem);	
	proc_destroy(child_proc);
	proc_table[pid] = NULL;
	return -pid;
}

int sys_execv(char *prog_name,char **args){
	
	int err, argc=0;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;		
	char **inargs;
	size_t str_len;
	int index=0;
	int strspace = 0;
	char name[PATH_MAX];
	
	err = copyinstr((userptr_t)prog_name, name, PATH_MAX, &str_len);
	if(err){
		return err;
	}
	str_len=0;
	while(1){
	
		inargs = kmalloc(sizeof(char**));
		err = copyin((const_userptr_t)args, inargs, sizeof(int));	
		if(err){
			kfree(inargs);
			return err;
		}	
		if(*inargs == NULL){
			kfree(inargs);
			break;
		}
		
		size_t len = 0;
		err = copyinstr((const_userptr_t)(*args), &buffer1[index],ARG_MAX-index, &len);
		if(err){
			kfree(inargs);
			return err;
		}
		index += len-1;
		buffer1[index++] = '\0';
		strspace += 1+(len/4);
		kfree(inargs);
		args++;
		argc++;
	}
	//array of pointers to each argument 
	int j = 0;
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
	
	stackptr -= strspace*4;
	stackptr -= 4*(argc+1);

	char *s_ptr = (char*)stackptr;

	s_ptr += 4*(argc);
	char *temp_ptr = s_ptr-4;
	char **ptrptr = &(temp_ptr);
	s_ptr += 4;
	j=0;
	*ptrptr = (char*)stackptr;
	for(int i=argc-1; i>=0; i--){
		
		str_len = strlen(&(buffer1[j]));
		err = copyout(&buffer1[j], (userptr_t)s_ptr, str_len);
		if(err){
			return err;
		}
		//refering to actual argument
		err = copyout(&s_ptr, (userptr_t)*ptrptr, sizeof(int));
		if(err){
			return err;
		}
		*ptrptr += 4;
		//padding nulls to the argument
		s_ptr += str_len;
		int nulls = 1+(str_len/4);
		nulls *= 4;
		nulls -= str_len;
		s_ptr += nulls;
		j+=str_len+1;
	}
	memset(buffer1, '\0', sizeof(buffer1));
	enter_new_process(argc, (userptr_t)stackptr, NULL, stackptr, entrypoint);
	panic("should not return here\n");	
	(void)inargs;
	(void)prog_name;
	(void)args;
	
	return 0;
}
