/**
 * Host shim for <dirent.h> — the host toolchain (MSVC) has no dirent.h.
 *
 * The UI "files" view browses the TF card with opendir()/readdir()/stat().
 * On the host there is no /sdcard, so sim/stubs/fake_fs.cpp serves a small
 * in-memory directory tree rooted at "/sdcard" that matches the sample data
 * used by the reference screenshots. Real Windows paths fall through to
 * FindFirstFile so the shim stays harmless for anything else.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
    uint64_t d_ino;
    int64_t  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char d_name[260];
};

typedef struct sim_dir DIR;

DIR *opendir(const char *dirname);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
void rewinddir(DIR *dirp);

#ifdef __cplusplus
}
#endif
