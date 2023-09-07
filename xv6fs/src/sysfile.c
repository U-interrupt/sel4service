//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "defs.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd(int n, int *pfd, struct file **pf) {
  int fd;
  struct file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = curr()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int fdalloc(struct file *f) {
  int fd;
  // struct proc *p = myproc();

  // skip STDIN, STDOUT, STDERR
  for (fd = 3; fd < NOFILE; fd++) {
    if (curr()->ofile[fd] == 0) {
      curr()->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64 xv6fs_dup(void) {
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64 xv6fs_read(void) {
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64 xv6fs_write(void) {
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64 xv6fs_pread(void) {
  struct file *f;
  int n;
  uint64 p;
  uint64 off;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;

  argaddr(3, &off);
  if (fileseek(f, (off_t)off, SEEK_SET)) {
    return -1;
  }

  return fileread(f, p, n);
}

uint64 xv6fs_pwrite(void) {
  struct file *f;
  int n;
  uint64 p;
  uint64 off;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;

  argaddr(3, &off);
  if (fileseek(f, (off_t)off, SEEK_SET)) {
    return -1;
  }

  return filewrite(f, p, n);
}

uint64 xv6fs_close(void) {
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  curr()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64 xv6fs_fstat(void) {
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64 xv6fs_link(void) {
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  // begin_op();
  if ((ip = namei(old)) == 0) {
    // end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR) {
    iunlockput(ip);
    // end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  // end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  // end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp) {
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64 xv6fs_unlink(void) {
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  // begin_op();
  if ((dp = nameiparent(path, name)) == 0) {
    // end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip)) {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  // end_op();

  return 0;

bad:
  iunlockput(dp);
  // end_op();
  return -1;
}

static struct inode *create(char *path, short type, short major, short minor) {
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);
  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    // ilock(ip);
    // if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
    //   return ip;
    // iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0) {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  ip->size = 0;
  iupdate(ip);

  if (type == T_DIR) { // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp->nlink++; // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64 xv6fs_open() {
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -1;
  // begin_op();

  printf("[xv6fs] open %s 0o%o\n", path, omode);

  if (omode & O_CREAT) {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0) {
      // end_op();
      return -1;
    }
  } else {
    if ((ip = namei(path)) == 0) {
      // end_op();
      return -1;
    }
    ilock(ip);
    // if (ip->type == T_DIR && omode != O_RDONLY) {
    //   iunlockput(ip);
    // end_op();
    // return -1;
    // }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    iunlockput(ip);
    // end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      fileclose(f);
    iunlockput(ip);
    // end_op();
    return -1;
  }

  if (ip->type == T_DEVICE) {
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
    itrunc(ip);
  }

  iunlock(ip);
  // end_op();

  return fd;
}

uint64 xv6fs_mkdir(void) {
  char path[MAXPATH];
  struct inode *ip;

  // begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
    // end_op();
    return -1;
  }
  iunlockput(ip);
  // end_op();
  return 0;
}

uint64 xv6fs_mknod(void) {
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  // begin_op();
  argint(1, &major);
  argint(2, &minor);
  if ((argstr(0, path, MAXPATH)) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0) {
    // end_op();
    return -1;
  }
  iunlockput(ip);
  // end_op();
  return 0;
}

uint64 xv6fs_chdir(void) {
  char path[MAXPATH];
  struct inode *ip;
  // struct proc *p = myproc();

  // begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) {
    // end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR) {
    iunlockput(ip);
    // end_op();
    return -1;
  }
  iunlock(ip);
  iput(curr()->cwd);
  // end_op();
  curr()->cwd = ip;
  strncpy(curr()->cwd_path, path, MAXPATH);
  return 0;
}

uint64 xv6fs_getcwd(void) {
  uint64 path;
  size_t size;

  argaddr(0, &path);
  argaddr(1, &size);

  strcpy((char *)path, curr()->cwd_path);
  return 0;
}

uint64 xv6fs_lstat(void) {
  char path[MAXPATH];
  uint64 buf;
  struct inode *ip;
  int n;

  argaddr(1, &buf);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -EINVAL;

  printf("[xv6fs] lstat %s\n", path);

  if ((ip = namei(path)) == 0) {
    return -ENOENT;
  }

  stati(ip, (struct stat *)buf);

  return 0;
}

uint64 xv6fs_lseek(void) {
  struct file *f;
  uint64 off;
  int whence;

  argaddr(1, &off);
  argint(2, &whence);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return fileseek(f, (off_t)off, whence);
}

// uint64 sys_pipe(void) {
//   uint64 fdarray; // user pointer to array of two integers
//   struct file *rf, *wf;
//   int fd0, fd1;
//   struct proc *p = myproc();

//   argaddr(0, &fdarray);
//   if (pipealloc(&rf, &wf) < 0)
//     return -1;
//   fd0 = -1;
//   if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
//     if (fd0 >= 0)
//       p->ofile[fd0] = 0;
//     fileclose(rf);
//     fileclose(wf);
//     return -1;
//   }
//   if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
//       copyout(p->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1))
//       <
//           0) {
//     p->ofile[fd0] = 0;
//     p->ofile[fd1] = 0;
//     fileclose(rf);
//     fileclose(wf);
//     return -1;
//   }
//   return 0;
// }
