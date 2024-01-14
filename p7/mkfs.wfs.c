#include "wfs.h"
#include <stdlib.h>
#include <fcntl.h> 
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("wrong usage\n");
        return 1;
    }
    char *disk_path = argv[1];
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("");
        return errno;
    }
    struct stat st;
    stat(disk_path, &st);
    struct wfs_sb sb = {
        .magic = WFS_MAGIC,
        .head = 0
    };
    void* disk = mmap(0, st.st_size, PROT_WRITE, MAP_SHARED|MAP_PRIVATE, fd, 0);
    struct wfs_inode root_inode = {
        .inode_number = 0,
        .deleted = 0,
        .mode = __S_IFDIR | 0755,
        .uid = st.st_uid,
        .gid = st.st_gid,
        .flags = 0,
        .size = 0,
        .atime = (unsigned)time(NULL),
        .mtime = (unsigned)time(NULL),
        .ctime = (unsigned)time(NULL),
        .links = 2
    };
    struct wfs_log_entry root_log = {
        .inode = root_inode
    };
    sb.head = sizeof(struct wfs_sb) + sizeof(struct wfs_log_entry);
    //printf("root inode: %d\n", root_inode.inode_number);
    memcpy(disk, &sb, sizeof(struct wfs_sb));
    // printf("ok\n");
    memcpy((void*)((char*)disk + sizeof(sb)), &root_log, sizeof(struct wfs_log_entry));
    // printf("disk address: %p\n", disk);
    munmap(disk, st.st_size);
    close(fd);
}