
int sys_write(int fd, void*  buf, int buflen);
int sys_open(char *filename,int flags);
int sys_read(int fd, void* buf, int buflen);
off_t sys_lseek(int fd,int low_32,int high_32,int whence);
int sys_dup2(int oldfd,int newfd);
int sys_getcwd(char *buf,size_t buflen);
