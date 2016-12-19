#define FUSE_USE_VERSION 30

#include <config.h>

#include "common.h"

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>

#include "spdk_interface.h"
#include "file_dev.h"
#include "namei.h"
/*
  * Command line options
  *
  * We can't set default values for the char* fields here because
  * fuse_opt_parse would attempt to free() them when the user specifies
  * different values on the command line.
  */
static struct options {
	const char *filename;
	const char *contents;
	int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--name=%s", filename),
	OPTION("--contents=%s", contents),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

static void *
stor_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	(void) conn;

	printf("enter into stor_init();\n");

	cfg->kernel_cache = 1;
	if (spdk_init() != 0) 
		exit(1);
	
	printf("come out   stor_init();\n");
	return NULL;
}

static void 
stor_destroy(void *value)
{
	printf("enter into stor_destroy();\n");
	
	spdk_cleanup();

	printf("come out   stor_destroy();\n");
}

static int 
stor_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	(void) fi;
	int res = 0;
	struct d_inode dir;	

	printf("enter into stor_getattr();\n");

	memset(stbuf, 0, sizeof(struct stat));
	/*if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path+1, options.filename) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(options.contents);
	} else
		res = -ENOENT;*/
	if (open_namei(path, O_RDWR, DIR_INODE, &dir))
		return -ENOENT;
	if ( dir.i_mode == DIR_INODE) {
		stbuf->st_mode = S_IFDIR | 0755; 
		stbuf->st_size = dir.i_tsize;
	} else if ( dir.i_mode == COMMON_INODE) {
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = dir.i_tsize;	
	}
	stbuf->st_nlink = 1;

	printf("come out   stor_getattr();\n");
	return res;
}

static int 
stor_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;
	char (*dir_list)[NAME_LEN] ;
	struct d_inode dir;	
	int size, i;

	printf("enter into stor_readdir();\n");

    if (open_namei(path, O_RDWR, DIR_INODE, &dir))
        return -ENOENT;

    size = (dir.i_tsize + DIR_ENTRY_SIZE - 1) / DIR_ENTRY_SIZE;
    dir_list = (char (*)[NAME_LEN])malloc(size * NAME_LEN * sizeof(char));

    if (!get_dir_entry_list(&dir, dir_list))
        return -99;
	
	/*if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);*/
	filler(buf, options.filename, NULL, 0, 0);
	
	/*if ((error = get_dir_list(path, dir_list, &size))){
		printf("size = %d\nerror = %d", size, error);
		return error;
	}*/
	
	for (i = 0; i < size; i++){
		//printf("dir_list[%d] = %s\n", i, dir_list[i]);
		filler(buf, dir_list[i], NULL, 0, 0); 
	}
	free(dir_list);
	//printf("size = %d\n", size);
	printf("come out   stor_readdir();\n");
	return 0;
}

static int
stor_mkdir(const char *path, mode_t mode)
{
	int res;
	
	printf("enter into stor_mkdir();\n");
	res = sys_mkdir(path);
	if (res != 0)
		return -errno;
	
	printf("come out   stor_mkdir();\n");
	return 0;
}

static int 
stor_open(const char *path, struct fuse_file_info *fi)
{
	struct d_inode node;

	printf("enter into stor_open();\n");

	/*if (strcmp(path+1, options.filename) != 0)
		return -ENOENT;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;
	*/
	if (open_namei(path, O_RDWR, DIR_INODE, &node) == -ENOENT){
		sys_mknod(path);
	} 
	
	printf("come out  stor_open();\n");
	return 0;
}

static int
stor_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
	printf("enter into stor_truncate();\n");
	if (sys_truncate(path, (unsigned long)size))
		return -ENOENT;
	printf("come out   stor_truncate();\n");
	return 0;
}

static int 
stor_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct d_inode node;

	printf("enter into stor_create();\n");	

	if (open_namei(path, O_RDWR, DIR_INODE, &node) == -ENOENT){
        sys_mknod(path);
    }

	printf("come out  stor_create();\n");
	return 0;
}

static int 
stor_read(const char *path, char *buf, size_t size, off_t offset,
	      struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;
	struct d_inode node;
	struct file filp;
	
	printf("enter into stor_read();\n");	
	printf("stor_read()::path=%s\n", path);

	if (open_namei(path, O_RDWR, DIR_INODE, &node))
        return -ENOENT;

	filp.f_mode = node.i_mode;
	filp.f_flags = node.i_mode;
	filp.f_inode = &node;
	filp.f_pos = (unsigned long) offset;

	if (node.i_uid != 99) {
		len = file_read(&node, &filp, buf, size);	
	} else {
	/*if(strcmp(path+1, options.filename) != 0)
		return -ENOENT;
	*/
		len = strlen(options.contents);
		if (offset < (off_t)len) {
			if (offset + size > len)
				size = len - offset;
			memcpy(buf, options.contents + offset, size);
		} else
			size = 0;
	}
	printf("come out   stor_read();\n");
	return len;
}

static int
stor_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	size_t len;
	struct d_inode node;
	struct file filp;
	
	printf("enter into stor_write();\n");	
	printf("stor_write()::path=%s\n", path);
	
	if (open_namei(path, O_RDWR, DIR_INODE, &node))
        return -ENOENT;
	
	filp.f_mode = node.i_mode;
	filp.f_flags = node.i_mode;
	filp.f_inode = &node;
	filp.f_pos = (unsigned long) offset;

	len = file_write(&node, &filp, (char *)buf, size);

	//set_inode(node.i_cinode, &node);	
	return len;
	printf("come out   stor_write();\n");
}

static int
stor_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	printf("enter into stor_fsync();\n");
	printf("come out   stor_fsync();\n");
	return 0;
}

static struct fuse_operations stor_oper = {
	.init		= stor_init,
	.destroy	= stor_destroy,
	.getattr	= stor_getattr,
	.readdir	= stor_readdir,
	.mkdir		= stor_mkdir,
	.open		= stor_open,
	.truncate	= stor_truncate,
	.create		= stor_create,
	.read		= stor_read,
	.write		= stor_write,
	.fsync		= stor_fsync,
};

static void 
show_help(const char *progname)
{
	printf("usage: %s [options] <mountpoint>\n\n", progname);
	printf("File-system specific options:\n"
	       "    --name=<s>          Name of the \"stor\" file\n"
	       "                        (default: \"stor\")\n"
	       "    --contents=<s>      Contents \"stor\" file\n"
	       "                        (default \"Hello, World!\\n\")\n"
	       "\n");
}

int 
main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	options.filename = strdup("README.txt");
	options.contents = strdup("Author:ystu\n");

	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0] = (char*) "";
	}

	return fuse_main(args.argc, args.argv, &stor_oper, NULL);
}
