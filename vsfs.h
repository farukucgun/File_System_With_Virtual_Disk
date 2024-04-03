

// Do not change this file //

#define MODE_READ 0
#define MODE_APPEND 1
#define BLOCKSIZE 2048 // bytes

int vsformat (char *vdiskname, unsigned int m);

int vsmount (char *vdiskname);

int vsumount ();

int vscreate(char *filename);

int vsopen(char *filename, int mode);

int vsclose(int fd);

int vssize (int fd);

int vsread(int fd, void *buf, int n);

int vsappend(int fd, void *buf, int n);

int vsdelete(char *filename);


