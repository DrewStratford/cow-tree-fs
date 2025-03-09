//#define FUSE_USE_VERSION 31
#include "definitions.h"
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)
#define _FILE_OFFSET_BITS 64
#include <fuse3/fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include <cstring>
#include <string>

#include <optional>

#include "file_system.h"
#include "page_allocator.h"
#include "BTree.h"

void create_file_system(BufferAllocator& ba, size_t total_pages) {
	auto sb_raw = ba.load(0);
	auto sb = (SuperBlock*)sb_raw.data();
	// Write super block
	*sb = SuperBlock {
		.next_key = 1,
		.free_list = {
			.total_pages = total_pages,
			.next_free = 0,
			.highest_unallocated = 1*PAGE_SIZE,
		},
	};
	sb_raw.set_dirty();

	auto initial_root = new_empty_leaf(ba);
	sb->tree_root = initial_root.id();
}

std::optional<BlockID> lookup(BufferAllocator& ba, KeyId key) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto result = search_btree(ba, super_block->tree_root, key);
	return result;
}

std::optional<BlockID> remove(BufferAllocator& ba, KeyId key) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	std::unordered_set<BlockID> to_free;
	auto propagation = delete_btree(ba, to_free, super_block->tree_root, key);

	if (propagation.did_modify) {
		auto new_root_raw = propagation.new_child;
		auto new_root = (BTNode*) new_root_raw.data();
		if (!new_root->header.is_leaf && new_root->header.count == 1) {
			to_free.insert(new_root_raw.id());
			super_block->tree_root = new_root->pairs[0].value;
		} else {
			super_block->tree_root = propagation.new_child.id();
		}
		free_pages(ba, to_free);
		super_block_raw.set_dirty();
		return propagation.deleted_value;
	}

	return {};
}

std::optional<BlockID> insert(BufferAllocator& ba, KeyId key, BlockID value) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto old_root = super_block->tree_root;
	std::unordered_set<BlockID> to_free;
	auto propagation = insert_btree(ba, to_free, super_block->tree_root, KeyPair {
				.key = key,
				.value = value,
			});

	if (propagation.is_split) {
		// Make a new root
		auto new_root_raw = new_empty_node(ba);
		auto new_root = (BTNode*)new_root_raw.data();
		new_root->header.count = 2;
		new_root->pairs[0] = KeyPair {
			.key = propagation.key,
			.value = propagation.left,
		};
		new_root->pairs[1] = KeyPair {
			.key = MAX_KEY_ID,
			.value = propagation.right,
		};
		new_root_raw.set_dirty();
		super_block->tree_root = new_root_raw.id();

	} else {
		super_block->tree_root = propagation.update;
	}

	// Free all of the copied blocks (in the future we could store these in a snapshot).
	to_free.insert(old_root);
	free_pages(ba, to_free);
	
	super_block_raw.set_dirty();

	if (propagation.did_replace) {
		return propagation.replaced;
	}
	return {};
}

/*
 * Directory stuff
 */

void Directory::insert_file(char* name, FSType type, BlockID block) {
	// TODO: this is totally unsafe
	auto dir_ent = (DirEntry*)&data[size];
	dir_ent->data = block;
	dir_ent->name_len = strlen(name);
	dir_ent->type = type;
	memcpy(dir_ent->name, name, dir_ent->name_len);
	size += sizeof(DirEntry) + dir_ent->name_len;
}

std::optional<KeyId> Directory::lookup_file(char* name) {
	auto name_len = strlen(name);
	DirEntry* dir_ent = nullptr;
	for (size_t i = 0; i < this->size; i+= sizeof(DirEntry) + dir_ent->name_len) {
		dir_ent = (DirEntry*)&data[size];
		if (strncmp(name, dir_ent->name, name_len) == 0) {
			auto out = dir_ent->data;
			return out;
		}
	}
	return {};
}

void Directory::list_contents() {
	DirEntry* dir_ent = nullptr;
	for (size_t i = 0; i < this->size; i+= sizeof(DirEntry) + dir_ent->name_len) {
		dir_ent = (DirEntry*)&data[i];
		switch (dir_ent->type) {
			case Unknown:
				printf("U\t");
				break;
			case SmallDir:
				printf("D\t");
				break;
			case SmallFile:
				printf("F\t");
				break;
		}
		printf("%ld, %.*s\n", 
				dir_ent->data, (int)dir_ent->name_len, dir_ent->name);
	}
}

void list_directory(BufferAllocator& ba, KeyId key) {
	auto parent_block = lookup(ba, key);
	if (!parent_block.has_value()) {
		return;
	}

	auto parent_raw = ba.load(parent_block.value());
	auto parent = (Directory*)parent_raw.data();
	parent->list_contents();
}

void inspect_block(BufferAllocator& ba, KeyId key) {
	auto parent_block = lookup(ba, key);
	if (!parent_block.has_value()) {
		printf("block does not exist\n");
		return;
	}

	auto parent_raw = ba.load(parent_block.value());
	auto parent = (FSHeader*)parent_raw.data();
	printf("key id %ld\n", parent->key);
	printf("block id %ld\n", parent->block);
	printf("type is ");
	switch (parent->type) {
			case Unknown:
				printf("U\n");
				break;
			case SmallDir:
				printf("D\n");
				break;
			case SmallFile:
				printf("F\n");
				break;
	}
}

void create_root_directory(BufferAllocator& ba) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	KeyId new_key = 1; // root is hardcoded to key 1
	super_block->next_key++;

	auto new_dir_raw = allocate_page(ba);
	auto new_dir = (Directory*)new_dir_raw.data();
	*new_dir = Directory{
		.header {
			.key = new_key,
			.block = new_dir_raw.id(),
			.type = SmallDir,
		},
	};
	
	insert(ba, new_key, new_dir_raw.id());

	super_block_raw.set_dirty();
	new_dir_raw.set_dirty();
}

std::optional<KeyId> add_directory(BufferAllocator& ba, KeyId parent_key, char* name) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto parent_block = lookup(ba, parent_key);
	if (!parent_block.has_value()) {
		return {};
	}

	auto parent_raw_old = ba.load(parent_block.value());
	auto parent_raw = allocate_page(ba);
	memcpy(parent_raw.data(), parent_raw_old.data(), PAGE_SIZE);
	auto parent = (Directory*)parent_raw.data();
	parent->header.block = parent_raw.id();
	// TODO: check parent type

	auto new_key = super_block->next_key;
	super_block->next_key++;

	auto new_dir_raw = allocate_page(ba);
	auto new_dir = (Directory*)new_dir_raw.data();
	*new_dir = Directory{
		.header {
			.key = new_key,
			.block = new_dir_raw.id(),
			.type = SmallDir,
		},
	};

	parent->insert_file(name, SmallDir, new_key);
	insert(ba, parent_key, parent_raw.id());
	insert(ba, new_key, new_dir_raw.id());
	new_dir_raw.set_dirty();
	parent_raw.set_dirty();

	super_block_raw.set_dirty();
	return new_key;
}

std::optional<KeyId> add_file(BufferAllocator& ba, KeyId parent_key, char* name) {
	auto super_block_raw = ba.load(0);
	SuperBlock* super_block = (SuperBlock*)super_block_raw.data();

	auto parent_block = lookup(ba, parent_key);
	if (!parent_block.has_value()) {
		return {};
	}

	auto parent_raw_old = ba.load(parent_block.value());
	auto parent_raw = allocate_page(ba);
	memcpy(parent_raw.data(), parent_raw_old.data(), PAGE_SIZE);
	auto parent = (Directory*)parent_raw.data();
	parent->header.block = parent_raw.id();
	// TODO: check parent type

	auto new_key = super_block->next_key;
	super_block->next_key++;

	auto new_file_raw = allocate_page(ba);
	auto new_file = (File*)new_file_raw.data();
	*new_file = File{
		.header {
			.key = new_key,
			.block = new_file_raw.id(),
			.type = SmallFile,
		},
	};

	parent->insert_file(name, SmallFile, new_key);
	insert(ba, parent_key, parent_raw.id());
	insert(ba, new_key, new_file_raw.id());
	new_file_raw.set_dirty();
	parent_raw.set_dirty();

	super_block_raw.set_dirty();
	return new_key;
}

void read_file(BufferAllocator& ba, KeyId key) {

	auto file_raw_old_ = lookup(ba, key);
	if (!file_raw_old_.has_value()) {
		return;
	}
	auto file_raw_old = ba.load(file_raw_old_.value());
	auto file = (File*)file_raw_old.data();
	file->read();
}

void write_file(BufferAllocator& ba, KeyId key,
		char* data, size_t len, size_t pos) {

	auto file_raw_old_ = lookup(ba, key);
	if (!file_raw_old_.has_value()) {
		return;
	}
	auto file_raw_old = ba.load(file_raw_old_.value());

	auto file_raw = allocate_page(ba);
	memcpy(file_raw.data(), file_raw_old.data(), PAGE_SIZE);
	auto file = (File*)file_raw.data();

	file->write(data, len, pos);
	file_raw.set_dirty();

	insert(ba, key, file_raw.id());
}

void File::write(char* data, size_t len, size_t pos) {
	// TODO: this is unsafe
	
	//make room for data
	for (size_t i = MAX_FILE_DATA-1; i > pos; i--) {
		this->data[i] = this->data[i-pos];
	}

	for (size_t i = pos; i < len && i < MAX_FILE_DATA; i++) {
		this->data[i] = data[i-pos];
	}
	this->size+= len;
};

void File::append(char* data, size_t len) {
	this->write(data, len, this->size);
};

void File::read() {
	// This is unsafe
	printf("%.*s\n", (int)this->size, this->data);
};

// Taken from the fuse low level example
//
// TODO: I can't work out a good way to make this non global
// in the low level API, so we're going to have to make it global
// for now.

BufferAllocator* global_ba;

BufferAllocator& get_ba() {
	if (global_ba) return *global_ba;
	FILE* f = fopen("/home/drew/src/cow-fs/test.dat", "r+");
	if (!f) return *global_ba;
	global_ba = new BufferAllocator(f, 100);
	return *global_ba;

}
static void cowfs_init(void *userdata, struct fuse_conn_info *conn)
{
	get_ba();

	/* Disable the receiving and processing of FUSE_INTERRUPT requests */
	//conn->no_interrupt = 1;
}

static void cowfs_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	printf("cowfs_getattr\n");
	auto block = lookup(*global_ba, (KeyId)ino);
	if (!block.has_value()) {
		printf("block id %ld\n not found\n", ino);
		fuse_reply_err(req, EISDIR);
		return;
	}
	auto file_raw = global_ba->load(block.value());
	auto file = (FSHeader*)file_raw.data();

	printf("loaded inode %ld\n", ino);
	struct stat e;
	memset(&e, 0, sizeof(e));
	e.st_ino = ino;

	// We don't properly store the file type in the FSHeader
	switch (file->type) {
		case Unknown:
			printf("actually unknown\n");
			// TODO: error 
			break;
		case SmallFile:
			{
				auto f = (File*)file;
				e.st_mode = S_IFREG | 0444;
				e.st_nlink = 1;
				e.st_size = f->size;
				printf("is_smallfile\n");
				break;
			}
		case SmallDir:
			e.st_mode = S_IFDIR | 0755;
			e.st_nlink = 2;
			printf("is_dir\n");
			break;
	}

	fuse_reply_attr(req, &e, 1.0);
}

static void cowfs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	printf("looking up %s\n", name);
	std::string path = name;

	auto parent_block = lookup(*global_ba, (KeyId)parent);
	if (!parent_block.has_value()) {
		return;
	}
	auto dir_raw = global_ba->load(parent_block.value());
	auto dir = (Directory*)dir_raw.data();

	DirEntry* dir_ent = nullptr;
	for (size_t i = 0; i < dir->size; i+= sizeof(DirEntry) + dir_ent->name_len) {
		dir_ent = (DirEntry*)&dir->data[i];
		std::string item_name(
				reinterpret_cast<const char *>(dir_ent->name),
				dir_ent->name_len);
		
		if (name == item_name) {
			struct fuse_entry_param e;
			memset(&e, 0, sizeof(e));
			e.ino = dir_ent->data;
			// TODO: timeouts could be higher
			e.attr_timeout = 1.0;
			e.entry_timeout = 1.0;
			e.attr.st_ino = e.ino;

			switch (dir_ent->type) {
				case SmallDir:
					printf("lookup is smalldir\n");
					e.attr.st_mode = S_IFDIR | 0755;
					e.attr.st_nlink = 2;
					break;
				case Unknown:
					printf("unknown dir entry!!\n");
					break;
				case SmallFile:
					printf("lookup is smallfile\n");
					e.attr.st_mode = S_IFREG | 0444;
					e.attr.st_nlink = 1;
					// lookup file to get the length
					{
						auto block = lookup(*global_ba, dir_ent->data);
						if (!block.has_value()) {
							// TODO: throw error
						}
						auto raw_file = global_ba->load(block.value());
						auto file = (File*)raw_file.data();
						e.attr.st_size = file->size;
					}
					break;
			}

			fuse_reply_entry(req, &e);
			return;
		}
	}
	fuse_reply_err(req, ENOENT);
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, DirEntry* dir)
{
	std::string name(reinterpret_cast<const char *>(dir->name), dir->name_len);
	printf("===> name is %s\n", name.c_str());
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name.c_str(), NULL, 0);
	b->p = (char *) realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = dir->data;
	//stbuf.st_mode = S_IFREG;
	printf("st_ino %lx\n", dir->data);
		
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name.c_str(), &stbuf,
			  b->size);
}


static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
			     off_t off, size_t maxsize)
{
	printf ("bufsize %ld, offset %ld, maxsize %ld", bufsize, off, maxsize);
	if (off < bufsize) {
		puts("returning something");
		return fuse_reply_buf(req, buf + off, std::min(bufsize - off, maxsize));
	}
	else {

		puts("returning nothing");
		return fuse_reply_buf(req, NULL, 0);
	}
}

static void cowfs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	printf("looking up dir %ld\n",ino);
	printf("global_ba %p\n", global_ba);
	auto parent_block = lookup(*global_ba, (KeyId)ino);
	if (!parent_block.has_value()) {
		printf("return nothing\n");
		fuse_reply_err(req, ENOTDIR);
		return;
	}

	printf("got parent block %ld\n", parent_block.value());
	auto parent_raw = global_ba->load(parent_block.value());
	auto dir = (Directory*)parent_raw.data();
	if (dir->header.type != FSType::SmallDir) {
		fuse_reply_err(req, ENOTDIR);
		return;
	}

	dir->list_contents();

	struct dirbuf b;
	memset(&b, 0, sizeof(b));

	// TODO: handle not dir
	DirEntry* dir_ent = nullptr;
	for (size_t i = 0; i < dir->size; i+= sizeof(DirEntry) + dir_ent->name_len) {
		dir_ent = (DirEntry*)&dir->data[i];
		printf(" ===> %ld, %.*s\n", 
				dir_ent->data, (int)dir_ent->name_len, dir_ent->name);
		dirbuf_add(req, &b, dir_ent);
	}
	reply_buf_limited(req, b.p, b.size, off, size);
	free(b.p);
}

static void cowfs_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	printf("cowfs_open\n");
	fuse_reply_open(req, fi);
}

static void cowfs_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	printf("cowfs_read %ld size %ld off %ld\n", ino, size, off);
	auto block = lookup(*global_ba, (KeyId)ino);
	if (!block.has_value()) {
		printf("block id %ld\n not found\n", ino);
		// TODO proper error
		fuse_reply_err(req, EISDIR);
		return;
	}

	auto file_raw = global_ba->load(block.value());
	auto file = (FSHeader*)file_raw.data();

	printf("loaded inode %ld\n", ino);
	struct stat e;
	memset(&e, 0, sizeof(e));
	e.st_ino = ino;

	switch (file->type) {
		case SmallDir:
			e.st_mode = S_IFDIR | 0755;
			e.st_nlink = 2;
			printf("is_dir\n");
			fuse_reply_err(req, EISDIR);
			break;
		case Unknown:
			printf("actually unknown\n");
			fuse_reply_err(req, EISDIR);
			break;
		case SmallFile:
			auto f = (File*)file;

			e.st_mode = S_IFREG | 0444;
			e.st_nlink = 1;
			e.st_size = f->size;
			printf("is_smallfile %ld\n", f->size);
			for(int i = 0; i < f->size; i++) {
				printf("%d,", f->data[i]);
			}
			puts("");
			printf("====> %.*s\n", (int)f->size, f->data);
			reply_buf_limited(req, f->data, f->size, off, size);

			break;
	}

}

static void cowfs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
							  size_t size)
{
	fuse_reply_err(req, ENOTSUP);
}

static void cowfs_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
							  const char *value, size_t size, int flags)
{
	fuse_reply_err(req, ENOTSUP);
}

static void cowfs_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
	fuse_reply_err(req, ENOTSUP);
}

static void cowfs_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
			mode_t mode) {

	auto resp = add_directory(*global_ba, (KeyId)parent, (char*)name);
	if (!resp.has_value()) {
	}
	struct fuse_entry_param e;
	memset(&e, 0, sizeof(e));
	e.ino = resp.value();
	e.attr_timeout = 100;
	e.entry_timeout = 100;

	e.attr.st_ino  = resp.value();
	e.attr.st_size = 4096;
	fuse_reply_entry(req, &e);
}

static void cowfs_create(fuse_req_t req, fuse_ino_t parent, const char *name,
			 mode_t mode, struct fuse_file_info *fi) {
	auto resp = add_file(*global_ba, (KeyId)parent, (char*)name);
	if (!resp.has_value()) {
		//TODO: proper error handling
	}
	struct fuse_entry_param e;
	memset(&e, 0, sizeof(e));
	e.ino = resp.value();
	e.attr_timeout = 100;
	e.entry_timeout = 100;

	e.attr.st_ino  = resp.value();
	e.attr.st_size = 0;

	fi->fh = e.ino;
	fuse_reply_create(req, &e, fi);
}

static void cowfs_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
			size_t size, off_t offset,
			[[maybe_unused]] struct fuse_file_info *fi) {
	write_file(*global_ba, (KeyId)ino, (char*)buf, size, offset);
	fuse_reply_write(req, size);
}

static const struct fuse_lowlevel_ops cowfs_oper = {
	.init = cowfs_init,
	.lookup = cowfs_lookup,
	.getattr = cowfs_getattr,
	.mkdir = cowfs_mkdir,
	.open = cowfs_open,
	.read = cowfs_read,
	.write = cowfs_write,
	.readdir = cowfs_readdir,
	.setxattr = cowfs_setxattr,
	.getxattr = cowfs_getxattr,
	.removexattr = cowfs_removexattr,
	.create = cowfs_create,
};

int fuse_start(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	struct fuse_loop_config *config;
	int ret = -1;

	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	if (opts.show_help) {
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		fuse_cmdline_help();
		fuse_lowlevel_help();
		ret = 0;
		goto err_out1;
	} else if (opts.show_version) {
		printf("FUSE library version %s\n", fuse_pkgversion());
		fuse_lowlevel_version();
		ret = 0;
		goto err_out1;
	}

	if(opts.mountpoint == NULL) {
		printf("usage: %s [options] <mountpoint>\n", argv[0]);
		printf("       %s --help\n", argv[0]);
		ret = 1;
		goto err_out1;
	}

	se = fuse_session_new(&args, &cowfs_oper,
			      sizeof(cowfs_oper), NULL);
	if (se == NULL)
	    goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
	    goto err_out2;

	if (fuse_session_mount(se, opts.mountpoint) != 0)
	    goto err_out3;

	fuse_daemonize(opts.foreground);

	/* Block until ctrl+c or fusermount -u */
	if (opts.singlethread)
		ret = fuse_session_loop(se);
	else {
		config = fuse_loop_cfg_create();
		fuse_loop_cfg_set_clone_fd(config, opts.clone_fd);
		fuse_loop_cfg_set_max_threads(config, opts.max_threads);
		ret = fuse_session_loop_mt(se, config);
		fuse_loop_cfg_destroy(config);
		config = NULL;
	}

	fuse_session_unmount(se);
err_out3:
	fuse_remove_signal_handlers(se);
err_out2:
	fuse_session_destroy(se);
err_out1:
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	return ret ? 1 : 0;
}
