#include <proc.h>
#include <mips/trapframe.h>
#include <limits.h>
#include <synch.h>

int sys_write(int fd,const void*  buf, int buflen);
int sys_open(char *filename,int flags);
int sys_read(int fd, void* buf, int buflen);
int sys_close(int fd);
int sys_chdir(const void *pathname);
off_t sys_lseek(int fd,int low_32,int high_32,const void *whence);
int sys_dup2(int oldfd,int newfd);
int sys_getcwd(char *buf,size_t buflen);
int sys_fork(struct trapframe *tf);
int sys_getpid(void);
int sys_waitpid(int pid, void* status, int options);
void sys__exit(int exitcode);
int sys_execv(char *prog_name, char **args);
int sys_sbrk(intptr_t amount, int *retval);

//struct args_buffer{
//
//	char buffer[ARG_MAX];
//	int curindex;
//	struct lock *buffer_lock;
//};

char buffer1[ARG_MAX];
//struct args_buffer *buffer;
