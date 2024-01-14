#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "wfs.h"
#include <sys/mman.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>

/**
 1. check for deadbeef
 2. open, do mmap, update struct
 3. create fuse_operations struct
 4. implement functions in the struct
 5. call fuse_main
*/
struct wfs_private* wp;
//struct wfs_log_entry* file_entries[256];
//struct wfs_dentry* dir_entries[256];
char dir_list[256][256];
int curr_dir_idx = -1;
int cur_inode_num = 100;

char files_list[256][256];
int curr_file_idx = -1;

char files_content[256][256];
int curr_file_content_idx = -1;

struct wfs_inode* number_to_inode(int inode_num){
    struct wfs_log_entry* start_log = (struct wfs_log_entry*)((char*)wp->disk + sizeof(struct wfs_sb));
    //printf("magic number in NTI: %u\n", ((struct wfs_sb*)(start_log))->magic);
    struct wfs_log_entry* end_log = (struct wfs_log_entry*)((char*)wp->disk + wp->head);
    struct wfs_log_entry* cur_log = NULL;
    while(start_log < end_log){
        if(start_log->inode.inode_number == inode_num){
            cur_log = start_log;
        }
        start_log = (struct wfs_log_entry*)((char*)start_log + sizeof(struct wfs_inode) + start_log->inode.size); 
    }
    if(cur_log == NULL || cur_log->inode.deleted){
        return NULL;
    }
    return (void*)cur_log;
}

struct wfs_inode* path_to_inode(const char *path){
     if(!strcmp(path, "/")){
        return number_to_inode(0);
    }
    struct wfs_log_entry* root_log = (void*)number_to_inode(0);
    char path_cpy[128];
    strncpy(path_cpy, path, sizeof(path_cpy));
    path_cpy[sizeof(path_cpy) - 1] = '\0';
    char *tok = strtok(path_cpy, "/");
    while(tok != NULL){
        int j = -1;
        struct wfs_dentry* dentry = (void*)root_log->data;
        int dentries = root_log->inode.size / sizeof(struct wfs_dentry);
        for(int i = 0; i < dentries; i++){
            if(strcmp(dentry[i].name, tok) == 0){
                j = dentry[i].inode_number;
                break;
            }
        }
        if(j == -1){
            return NULL;
        }
        root_log = (void*)number_to_inode(j);
        tok = strtok(NULL, "/");
    }
    return (struct wfs_inode*)root_log;
}

void add_dir(const char *dir_name)
{
	curr_dir_idx++;
	strcpy(dir_list[curr_dir_idx], dir_name);
}

int is_dir(const char *path)
{
	path++; // Eliminating "/" in the path
	
	for (int curr_idx = 0; curr_idx <= curr_dir_idx; curr_idx++)
		if ( strcmp(path, dir_list[curr_idx] ) == 0)
			return 1;
	
	return 0;
}

void add_file(const char *filename)
{
	curr_file_idx++;
	strcpy(files_list[ curr_file_idx ], filename);
	
	curr_file_content_idx++;
	strcpy(files_content[ curr_file_content_idx ], "");
}

int get_file_index(const char *path)
{
	path++; // Eliminating "/" in the path
	
	for (int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++)
		if ( strcmp(path, files_list[curr_idx]) == 0)
			return curr_idx;
	
	return -1;
}

int is_file( const char *path )
{
	path++; // Eliminating "/" in the path
	
	for ( int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++ )
		if ( strcmp( path, files_list[ curr_idx ] ) == 0 )
			return 1;
	
	return 0;
}

int write_to_file(const char *path, const char *new_content)
{
	int file_idx = get_file_index(path);
	
	if (file_idx == -1) // No such file
		return -1;
		
	strcpy(files_content[file_idx], new_content); 
    return 0;
}

static int wfs_getattr(const char *path, struct stat *st){
    // if (strcmp(path, "/") == 0 || is_dir(path) == 1){
	// 	st->st_mode = S_IFDIR | 0755;
	// 	st->st_nlink = 2;
	// }
	// else if(is_file(path) == 1){
	// 	st->st_mode = S_IFREG | 0644;
	// 	st->st_nlink = 1;
	// 	st->st_size = 1024;
	// }else{
	// 	return -ENOENT;
	// }
    // printf("getattr\n");
    struct wfs_inode* cur_inode = path_to_inode(path);
    if(cur_inode == NULL){
       return -ENOENT; 
    }
    st->st_mode = cur_inode->mode;
    st->st_uid = cur_inode->uid;
    st->st_gid = cur_inode->gid;
	st->st_atime = cur_inode->atime;
	st->st_mtime = cur_inode->mtime;
    st->st_size = cur_inode->size;
    st->st_nlink = cur_inode->links;
    return 0;
}

static int wfs_mknod (const char *path, mode_t mode, dev_t dev){
    printf("mknod\n");
    char* temp_path1 = strdup(path);
    char* temp_path2 = strdup(path);
    char* dir_path = dirname(temp_path1);
    char* base_name = basename(temp_path2);
	void* disk = wp->disk;
    // get parent inode
    struct wfs_inode* parent_inode = path_to_inode(dir_path);
    // get parent log entry
    //memcpy(&new_parent_entry->inode, parent_inode, sizeof(struct wfs_inode));
    struct wfs_inode new_parent_inode = {
        .inode_number = parent_inode->inode_number,
        .deleted = 0,
        .mode = parent_inode->mode,
        .uid = parent_inode->uid,
        .gid = parent_inode->gid,
        .size = parent_inode->size,
        .atime = parent_inode->atime,
        .ctime = parent_inode->ctime,
        .links = parent_inode->links
    };
    struct wfs_log_entry* parent_entry = (struct wfs_log_entry*)parent_inode;
    struct wfs_log_entry* new_parent_entry = malloc(sizeof(struct wfs_inode) + parent_inode->size + sizeof(struct wfs_dentry));
    memcpy(new_parent_entry, &new_parent_inode, sizeof(struct wfs_inode));
    memcpy(&new_parent_entry->data, parent_entry->data, parent_inode->size);
    parent_inode->deleted = 1;
    // new_parent_entry->inode.atime = (unsigned)time(NULL);
    // new_parent_entry->inode.mtime = (unsigned)time(NULL);
    struct wfs_dentry* new_dentry = (struct wfs_dentry*)((char*)new_parent_entry + sizeof(struct wfs_inode) + parent_inode->size);
    cur_inode_num++;
    new_dentry->inode_number = cur_inode_num;
    strcpy(new_dentry->name, base_name);
    new_parent_entry->inode.size += sizeof(struct wfs_dentry);
    // wp->head += sizeof(struct wfs_dentry);
    memcpy((char*)wp->disk + wp->head, new_parent_entry, sizeof(struct wfs_inode) + parent_inode->size + sizeof(struct wfs_dentry));
    wp->head += sizeof(struct wfs_inode) + parent_inode->size + sizeof(struct wfs_dentry);
    // create new inode
    struct wfs_inode new_node = {
        .inode_number = cur_inode_num,
        .deleted = 0,
        .mode = __S_IFREG | mode,
        .uid = getuid(),
        .gid = getgid(),
        .flags = 0,
        .size = 0,
        .atime = (unsigned)time(NULL),
        .mtime = (unsigned)time(NULL),
        .ctime = (unsigned)time(NULL),
        .links = 1
    };
    // struct wfs_log_entry* new_log = malloc(sizeof(struct wfs_inode));
    // new_log->inode = new_node;
    // memcpy(new_log, &new_node, sizeof(struct wfs_inode));
    memcpy((struct wfs_log_entry*)((char*)(disk)+wp->head), &new_node, sizeof(struct wfs_inode));
    printf("parent inode: %d\n", parent_inode->inode_number);
    //printf("log node number: %d\n", new_log->inode.inode_number);
    // printf("dir_path: %s\n", dir_path);
    //memcpy((void*)((char*)disk+wp->head), new_log, sizeof(struct wfs_log_entry));
    wp->head += sizeof(struct wfs_inode);
    printf("ok\n");
    free(temp_path1);
    free(temp_path2);
	return 0;
}

static int wfs_mkdir (const char *path, mode_t mode){
    // if(is_dir(path)){
    //     return -EEXIST;
    // }
    // path++;  
    // add_dir(path);
    /**
     creat an empty inode
    */
    //struct wfs_dentry* dentry = malloc(sizeof(struct wfs_dentry));
    //strcpy(dentry->name, path);
    //dentry->inode_number = cur_inode;
    //dir_entries[curr_dir_idx] = dentry;
    printf("mkdir\n");
    char* temp_path1 = strdup(path);
    char* temp_path2 = strdup(path);
    char* dir_path = dirname(temp_path1);
    char* base_name = basename(temp_path2);
	void* disk = wp->disk;
    // get parent inode
    struct wfs_inode* parent_inode = path_to_inode(dir_path);
    // get parent log entry
    //memcpy(&new_parent_entry->inode, parent_inode, sizeof(struct wfs_inode));
    struct wfs_inode new_parent_inode = {
        .inode_number = parent_inode->inode_number,
        .deleted = 0,
        .mode = parent_inode->mode,
        .uid = parent_inode->uid,
        .gid = parent_inode->gid,
        .size = parent_inode->size,
        .atime = parent_inode->atime,
        .ctime = parent_inode->ctime,
        .links = parent_inode->links
    };
    struct wfs_log_entry* parent_entry = (struct wfs_log_entry*)parent_inode;
    struct wfs_log_entry* new_parent_entry = malloc(sizeof(struct wfs_inode) + parent_inode->size + sizeof(struct wfs_dentry));
    memcpy(new_parent_entry, &new_parent_inode, sizeof(struct wfs_inode));
    memcpy(&new_parent_entry->data, parent_entry->data, parent_inode->size);
    parent_inode->deleted = 1;
    // new_parent_entry->inode.atime = (unsigned)time(NULL);
    // new_parent_entry->inode.mtime = (unsigned)time(NULL);
    struct wfs_dentry* new_dentry = (struct wfs_dentry*)((char*)new_parent_entry + sizeof(struct wfs_inode) + parent_inode->size);
    cur_inode_num++;
    new_dentry->inode_number = cur_inode_num;
    strcpy(new_dentry->name, base_name);
    new_parent_entry->inode.size += sizeof(struct wfs_dentry);
    // wp->head += sizeof(struct wfs_dentry);
    memcpy((char*)wp->disk + wp->head, new_parent_entry, sizeof(struct wfs_inode) + parent_inode->size + sizeof(struct wfs_dentry));
    wp->head += sizeof(struct wfs_inode) + parent_inode->size + sizeof(struct wfs_dentry);
    // create new inode
    struct wfs_inode new_node = {
        .inode_number = cur_inode_num,
        .deleted = 0,
        .mode = __S_IFDIR | mode,
        .uid = getuid(),
        .gid = getgid(),
        .flags = 0,
        .size = 0,
        .atime = (unsigned)time(NULL),
        .mtime = (unsigned)time(NULL),
        .ctime = (unsigned)time(NULL),
        .links = 1
    };
    // struct wfs_log_entry* new_log = malloc(sizeof(struct wfs_inode));
    // new_log->inode = new_node;
    // memcpy(new_log, &new_node, sizeof(struct wfs_inode));
    memcpy((struct wfs_log_entry*)((char*)(disk)+wp->head), &new_node, sizeof(struct wfs_inode));
    printf("parent inode: %d\n", parent_inode->inode_number);
    //printf("log node number: %d\n", new_log->inode.inode_number);
    // printf("dir_path: %s\n", dir_path);
    //memcpy((void*)((char*)disk+wp->head), new_log, sizeof(struct wfs_log_entry));
    wp->head += sizeof(struct wfs_inode);
    printf("ok\n");
    free(temp_path1);
    free(temp_path2);
	return 0;
}

static int wfs_read (const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi){
    // int file_idx = get_file_index(path);
	
	// if ( file_idx == -1 )
	// 	return -1;
	
	// char *content = files_content[file_idx];
	
	// memcpy(buffer, content + offset, size);
    //printf("read\n");
    struct wfs_inode* cur_inode = path_to_inode(path);
    if (cur_inode == NULL){
        return -ENOENT;
    }
    size_t size_read = cur_inode->size - offset;
    if(size > size_read){
        size = size_read;
    }
    struct wfs_log_entry* cur_entry = (void*) cur_inode;
    char *content = cur_entry->data;
    memcpy(buffer, content + offset, size);
	return size;
}

static int wfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi){
    // if(size > wp->len){
    //     return -ENOSPC;
    // }
    printf("write\n");
    struct wfs_inode* cur_inode = path_to_inode(path);
    cur_inode->deleted = 1;
    if (cur_inode == NULL){
        return -ENOENT;
    } 
    size_t size_written = sizeof(buffer) - offset;
    if(size > size_written){
        size = size_written;
    }
    struct wfs_inode new_inode = {
        .inode_number = cur_inode -> inode_number,
        .deleted = 0,
        .mode = cur_inode -> mode,
        .uid = cur_inode -> uid,
        .gid = cur_inode -> gid,
        .flags = cur_inode -> flags,
        .size = size,
        .atime = (unsigned)time(NULL),
        .mtime = (unsigned)time(NULL),
        .ctime = (unsigned)time(NULL),
        .links = cur_inode -> links
    };
    struct wfs_log_entry* new_log = (struct wfs_log_entry*)((char*)wp->disk + wp->head);
    new_log -> inode = new_inode;
    memcpy(new_log->data, buffer + offset, size);
    wp->head += (sizeof(struct wfs_log_entry) + new_inode.size);
	return size;
}

static int wfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
    // if (strcmp(path, "/" ) == 0)
	// {
	// 	for (int curr_idx = 0; curr_idx <= curr_dir_idx; curr_idx++)
	// 		filler(buffer, dir_list[curr_idx], NULL, 0);
	
	// 	for (int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++)
	// 		filler(buffer, files_list[curr_idx], NULL, 0);
	// }
    //printf("readdir\n");
    filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

    struct wfs_inode* cur_inode = path_to_inode(path);
    struct wfs_log_entry* cur_dir = (void*)cur_inode;
    int num_dentries = cur_inode->size / sizeof(struct wfs_dentry); 
    struct wfs_dentry* cur_dentry = (void*)(cur_dir->data);
    for(int i = 0; i < num_dentries; i++){
        filler(buffer, (cur_dentry + i)->name, NULL, 0);
    }
    return 0;
}


static int wfs_unlink(const char *path){
    // printf("path: %s\n", path);
    // char *path_cpy = strdup(path);
    // char *parent_path = dirname(path_cpy);
    // struct wfs_inode* old_inode = path_to_inode(path);
    // struct wfs_inode* old_parent = path_to_inode(parent_path);
    // struct wfs_log_entry* old_entry = (struct wfs_log_entry*)old_parent;

    // old_inode->deleted = 1;
    // int deleted_num = old_inode->inode_number; 
    // int num_dentries = old_parent->size / sizeof(struct wfs_dentry); 
        
    // struct wfs_inode new_inode = {
    //     .inode_number = old_parent->inode_number,
    //     .deleted = 0,
    //     .mode = old_parent->mode,
    //     .uid = old_parent->uid,
    //     .gid = old_parent->gid,
    //     .size = old_parent->size - sizeof(struct wfs_dentry),
    //     .atime = old_parent->atime,
    //     .ctime = old_parent->ctime,
    //     .links = old_parent->links 
    // };

    // struct wfs_log_entry* new_log = malloc(sizeof(struct wfs_inode) + old_parent->size - sizeof(struct wfs_dentry));
    // memcpy(new_log, &new_inode, sizeof(struct wfs_inode));
    // old_parent->deleted = 1;
    // //wp->head += sizeof(struct wfs_inode);
    // struct wfs_dentry* cur_dentry = (struct wfs_dentry*)(new_log + sizeof(struct wfs_inode));
    // struct wfs_dentry* old_dentry = (void*)old_entry->data;
    // for(int i = 0; i < num_dentries; i++){
    //     if(old_dentry->inode_number != deleted_num){
    //         memcpy(cur_dentry, old_dentry, sizeof(struct wfs_dentry));
    //         cur_dentry += sizeof(struct wfs_dentry);
    //     }
    //     old_dentry += sizeof(struct wfs_dentry);
    // }
    // memcpy((char*)wp->disk + wp->head, new_log, sizeof(struct wfs_inode) + old_parent->size - sizeof(struct wfs_dentry));
    // wp->head += (sizeof(struct wfs_inode) + old_parent->size - sizeof(struct wfs_dentry));
    // free(path_cpy);
    return 0;
}

int main(int argc, char* argv[]) {
    char* disk_path = argv[argc-2];
    char* mount_point = argv[argc-1];
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    
    // printf("disk_path: %s\n", disk_path);
    // printf("mount_point: %s\n", mount_point);
    int fd = open(disk_path, O_RDWR);
    if (fd < 0) {
        perror("");
        return errno;
    }
    struct stat st;
    stat(disk_path, &st);
    void* disk = mmap(0, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    // printf("magic number: %u\n", ((struct wfs_sb*)(disk))->magic);
    // printf("deadbeef: %u\n", WFS_MAGIC);
    
    if (((struct wfs_sb*)(disk))->magic != WFS_MAGIC) {
        return 1;
    }
    wp = malloc(sizeof(struct wfs_private));
    wp->fd = fd;
    wp->disk = disk;
    wp->len = st.st_size;
    wp->head = ((struct wfs_sb*)(disk))->head;
    
    static struct fuse_operations ops = {
        .getattr	= wfs_getattr,
        .mknod      = wfs_mknod,
        .mkdir      = wfs_mkdir,
        .read	    = wfs_read,
        .write      = wfs_write,
        .readdir	= wfs_readdir,
        .unlink    	= wfs_unlink,
    };
    fuse_main(argc-1, argv, &ops, NULL);
    free(wp);
    return 0;
}