#include "atfile.h"

#ifdef __linux

#include <linux/limits.h> // PATH_MAX
#include <errno.h>        // EBADF and other error constants
#include <cstdio>         // snprintf (emulate libc only in terms of libc)
#include <unistd.h>       // non-*at versions of emulated functions, e.g. readlink

// Attempt to get path from file descriptor
inline static int path_from_fd(int fd, char *buf, size_t buf_sz) {
  if (fd < 0) return EBADF;
  // Attempt to get path from file descriptor using proc fs.
  // This is a best-effort approach: proc fs may be unavailable and
  // this method may not work on every type of fds.
  char pfs_path[PATH_MAX];
  snprintf(pfs_path, PATH_MAX, "/proc/self/fd/%d", fd);
  return ::readlink(pfs_path, buf, buf_sz);
}

// -- Compatibility mappings for supported functions ---
#if KJ_COMPAT_ATFILE_EMULATE_READLINKAT == 1
int kj::readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
  if (pathname == nullptr) return EINVAL;
  if (*pathname != '/') {    // glibc's way of determining path relativeness
    if (dirfd != AT_FDCWD) {
      if (dirfd < 0) return EBADF;
      char cwd_path[PATH_MAX];
      path_from_fd(dirfd, cwd_path, PATH_MAX);
      char path[PATH_MAX];
      snprintf(path, PATH_MAX, "%s/%s", cwd_path, pathname);
      return ::readlink(path, buf, bufsiz);
    }
  }
  return ::readlink(pathname, buf, bufsiz);
}
#endif // KJ_COMPAT_ATFILE_EMULATE_READLINKAT

#if KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT == 1
int kj::symlinkat(const char *oldpath, int newdirfd, const char *newpath) {
  if ((oldpath == nullptr) || (newpath == nullptr)) return EINVAL;
  if (*oldpath != '/') {
    if (newdirfd != AT_FDCWD) {
      if (newdirfd < 0) return EBADF;
      char cwd_path[PATH_MAX];
      char path[PATH_MAX];
      path_from_fd(newdirfd, cwd_path, PATH_MAX);
      snprintf(path, PATH_MAX, "%s/%s", cwd_path, newpath);
      return ::symlink(oldpath, path);
    }
  }
  return ::symlink(oldpath, newpath);
}
#endif // KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT

#if KJ_COMPAT_ATFILE_EMULATE_LINKAT == 1
int kj::linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int) {
  // This implementation ignores flags, similar to old Linux kernels
  if ((oldpath == nullptr) || (newpath == nullptr)) return EINVAL;
  char cwd_path[PATH_MAX];
  char src_path[PATH_MAX];
  char dst_path[PATH_MAX];
  if ((*oldpath != '/') && (olddirfd != AT_FDCWD)) {
    if (olddirfd < 0) return EBADF;
    path_from_fd(olddirfd, cwd_path, PATH_MAX);
    snprintf(src_path, PATH_MAX, "%s/%s", cwd_path, oldpath);
  } else {
    snprintf(src_path, PATH_MAX, "%s", oldpath);
  }
  if ((*newpath != '/') && (newdirfd != AT_FDCWD)) {
    if (newdirfd < 0) return EBADF;
    path_from_fd(newdirfd, cwd_path, PATH_MAX);
    snprintf(dst_path, PATH_MAX, "%s/%s", cwd_path, newpath);
  } else {
    snprintf(dst_path, PATH_MAX, "%s", newpath);
  }
  return ::link(src_path, dst_path);
}
#endif // KJ_COMPAT_ATFILE_EMULATE_LINKAT

#if KJ_COMPAT_ATFILE_EMULATE_MKNODAT == 1
int kj::mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev) {
  if (pathname == nullptr) return EINVAL;
  if (*pathname != '/') {
    if (dirfd != AT_FDCWD) {
      if (dirfd < 0) return EBADF;
      char cwd_path[PATH_MAX];
      path_from_fd(dirfd, cwd_path, PATH_MAX);
      char path[PATH_MAX];
      snprintf(path, PATH_MAX, "%s/%s", cwd_path, pathname);
      return ::mknod(path, mode, dev);
    }
  }
  return ::mknod(pathname, mode, dev);
}
#endif // KJ_COMPAT_ATFILE_EMULATE_MKNODAT

#endif // __linux
