/**
 * fake_fs.cpp — in-memory "/sdcard" for the host preview renderer.
 *
 * Serves <dirent.h> (opendir/readdir/rewinddir/closedir) and sim_fs_stat()
 * from a tiny static tree that mirrors the reference screenshots:
 *
 *   /sdcard
 *     ├── 周杰伦 - 经典无损合集/      (2 tracks)
 *     ├── DSD 母带高解析专区/         (2 tracks)
 *     ├── 欧美发烧经典原盘/           (2 tracks, incl. the now-playing file)
 *     ├── 00_系统开机欢迎音.wav
 *     ├── 01_加州旅馆 - Eagles (Live).flac
 *     ├── 02_Hotel California.flac
 *     └── 03_千千阙歌 - 陈慧娴.flac
 *
 * Nothing here touches the real disk; entries are synthesized on the fly.
 */

#include "dirent.h"
#include "sim_fs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

namespace {

struct FsEntry {
    const char *name;
    bool is_dir;
};

struct FsDir {
    const char *path;
    const FsEntry *entries;
    int count;
};

const FsEntry kRootEntries[] = {
    { "周杰伦 - 经典无损合集", true },
    { "DSD 母带高解析专区",    true },
    { "欧美发烧经典原盘",      true },
    { "00_系统开机欢迎音.wav", false },
    { "01_加州旅馆 - Eagles (Live).flac", false },
    { "02_Hotel California.flac", false },
    { "03_千千阙歌 - 陈慧娴.flac", false },
};

const FsEntry kJayEntries[] = {
    { "01_晴天.flac",        false },
    { "02_七里香.flac",      false },
};

const FsEntry kDsdEntries[] = {
    { "01_渡口.dsf",         false },
    { "02_被遗忘的时光.dsf", false },
};

const FsEntry kWestEntries[] = {
    { "01_加州旅馆 - Eagles (Live).flac", false },
    { "02_Hotel California.flac",        false },
};

const FsDir kDirs[] = {
    { "/sdcard",                         kRootEntries,  (int)(sizeof(kRootEntries)  / sizeof(kRootEntries[0])) },
    { "/sdcard/周杰伦 - 经典无损合集",    kJayEntries,   (int)(sizeof(kJayEntries)   / sizeof(kJayEntries[0])) },
    { "/sdcard/DSD 母带高解析专区",       kDsdEntries,   (int)(sizeof(kDsdEntries)   / sizeof(kDsdEntries[0])) },
    { "/sdcard/欧美发烧经典原盘",         kWestEntries,  (int)(sizeof(kWestEntries)  / sizeof(kWestEntries[0])) },
};

const FsDir *find_dir(const char *path)
{
    if (!path) return nullptr;
    for (size_t i = 0; i < sizeof(kDirs) / sizeof(kDirs[0]); i++) {
        if (strcmp(kDirs[i].path, path) == 0) return &kDirs[i];
    }
    return nullptr;
}

} /* namespace */

struct sim_dir {
    const FsDir *dir;
    int index;
    struct dirent ent;
};

extern "C" DIR *opendir(const char *dirname)
{
    const FsDir *d = find_dir(dirname);
    if (!d) {
        errno = ENOENT;
        return nullptr;
    }
    sim_dir *sd = (sim_dir *)calloc(1, sizeof(sim_dir));
    if (!sd) return nullptr;
    sd->dir = d;
    sd->index = 0;
    return (DIR *)sd;
}

extern "C" struct dirent *readdir(DIR *dirp)
{
    sim_dir *sd = (sim_dir *)dirp;
    if (!sd || !sd->dir) return nullptr;
    if (sd->index >= sd->dir->count) return nullptr;

    const FsEntry *e = &sd->dir->entries[sd->index++];
    memset(&sd->ent, 0, sizeof(sd->ent));
    snprintf(sd->ent.d_name, sizeof(sd->ent.d_name), "%s", e->name);
    sd->ent.d_type = e->is_dir ? 4 /* DT_DIR */ : 8 /* DT_REG */;
    return &sd->ent;
}

extern "C" void rewinddir(DIR *dirp)
{
    sim_dir *sd = (sim_dir *)dirp;
    if (sd) sd->index = 0;
}

extern "C" int closedir(DIR *dirp)
{
    free(dirp);
    return 0;
}

extern "C" int sim_fs_stat(const char *path, struct stat *buf)
{
    if (!path || !buf) {
        errno = EINVAL;
        return -1;
    }
    memset(buf, 0, sizeof(*buf));

    const FsDir *d = find_dir(path);
    if (d) {
        buf->st_mode = (unsigned short)(_S_IFDIR | 0755);
        buf->st_nlink = 2;
        buf->st_size = 0;
        return 0;
    }

    /* file inside one of the fake directories? */
    const char *slash = strrchr(path, '/');
    if (slash && slash != path) {
        char parent[512];
        size_t n = (size_t)(slash - path);
        if (n < sizeof(parent)) {
            memcpy(parent, path, n);
            parent[n] = '\0';
            const FsDir *pd = find_dir(parent);
            if (pd) {
                const char *name = slash + 1;
                for (int i = 0; i < pd->count; i++) {
                    if (!pd->entries[i].is_dir && strcmp(pd->entries[i].name, name) == 0) {
                        buf->st_mode = (unsigned short)(_S_IFREG | 0644);
                        buf->st_nlink = 1;
                        buf->st_size = 28 * 1024 * 1024; /* plausible FLAC size */
                        return 0;
                    }
                }
            }
        }
    }

    errno = ENOENT;
    return -1;
}
