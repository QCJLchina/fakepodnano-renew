/**
 * sim_fs.h — host file-system shim, force-included into firmware/main/ui/*.cpp
 * ==========================================================================
 * The device UI browses the TF card with POSIX opendir()/readdir()/stat() and
 * S_ISDIR(). MSVC provides neither <dirent.h> nor S_ISDIR, and "/sdcard" does
 * not exist on the host, so:
 *
 *   - sim/stubs/dirent.h      maps opendir/readdir onto an in-memory tree
 *   - this header             maps stat() onto the same tree and adds S_ISDIR()
 *
 * It is force-included (MSVC /FIsim_fs.h) so that view sources never need to
 * know about the shim. Real (non "/sdcard") paths simply report ENOENT.
 */
#pragma once

#include <sys/stat.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* stat() replacement; returns 0 on success, -1 on failure (errno = ENOENT). */
int sim_fs_stat(const char *path, struct stat *buf);

#ifdef __cplusplus
}
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

/* Route every stat() call in the compiled UI sources through the shim. */
#define stat(p, b) sim_fs_stat((p), (b))
