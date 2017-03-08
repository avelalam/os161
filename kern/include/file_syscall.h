#include <proc.h>

int sys_write(int fd,const void*  buf, int buflen);
int sys_open(char *filename,int flags);
int sys_read(int fd, void* buf, int buflen);
int sys_close(int fd);
int sys_chdir(const void *pathname);
off_t sys_lseek(int fd,int low_32,int high_32,const void *whence);
int sys_dup2(int oldfd,int newfd);
int sys_getcwd(char *buf,size_t buflen);
int sys_fork(struct proc *child);
