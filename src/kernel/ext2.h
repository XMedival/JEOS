#pragma once
#include "types.h"
#include "vfs.h"

/* ---- fs state / error action ---- */
#define EXT2_FS_STATE_CLEAN         1
#define EXT2_FS_STATE_ERR           2
#define EXT2_ERR_ACTION_IGNORE      1
#define EXT2_ERR_ACTION_READ_ONLY   2
#define EXT2_ERR_ACTION_PANIC       3

/* ---- creator OS IDs ---- */
#define EXT2_SYSTEM_LINUX           0
#define EXT2_SYSTEM_GNU_HURD        1
#define EXT2_SYSTEM_MASIX           2
#define EXT2_SYSTEM_FREE_BSD        3

/* ---- misc constants ---- */
#define EXT2_SIGNATURE              0xEF53
#define EXT2_ROOT_INO               2

/* ---- inode type bits (top nibble of mode) ---- */
#define EXT2_S_IFSOCK               0xC000
#define EXT2_S_IFLNK                0xA000
#define EXT2_S_IFREG                0x8000
#define EXT2_S_IFBLK                0x6000
#define EXT2_S_IFDIR                0x4000
#define EXT2_S_IFCHR                0x2000
#define EXT2_S_IFIFO                0x1000

#pragma pack(1)

/* Base superblock (84 bytes, at byte offset 1024 on disk) */
struct ext2_superblock {
    u32 total_inodes;
    u32 total_blocs;
    u32 su_blocks;
    u32 free_blocks;
    u32 free_inodes;
    u32 superblock_block;
    u32 block_size;         /* s_log_block_size: actual = 1024 << this */
    u32 fragment_size;
    u32 block_per_group;
    u32 fragment_per_group;
    u32 inodes_per_group;
    u32 mount_time;
    u32 write_time;
    u16 mounts_since_check;
    u16 mount_per_check;
    u16 signature;          /* must be EXT2_SIGNATURE */
    u16 fs_state;
    u16 error_action;
    u16 version_low;        /* s_minor_rev_level */
    u32 last_check_time;
    u32 check_interval;
    u32 system_id;
    u32 version_high;       /* s_rev_level: 0=original, 1=dynamic */
    u16 su_id;
    u16 su_group_id;
};

/* Extended superblock fields (version_high >= 1, immediately after base at offset 84) */
struct ext2_superblock_ext {
    u32  first_inode;       /* first non-reserved inode */
    u16  inode_size;        /* size of each inode in bytes */
    u16  bg_nr;             /* block group holding this superblock copy */
    u32  feat_compat;
    u32  feat_incompat;
    u32  feat_ro_compat;
    u8   uuid[16];
    char volume_name[16];
    char last_mnt[64];
    u32  algo_bitmap;
};

/* Block Group Descriptor (32 bytes) */
struct ext2_bgd {
    u32 block_bitmap;       /* block number of block usage bitmap */
    u32 inode_bitmap;       /* block number of inode usage bitmap */
    u32 inode_table;        /* block number of inode table start */
    u16 free_blocks;
    u16 free_inodes;
    u16 used_dirs;
    u16 _pad;
    u32 _reserved[3];
};

/* On-disk inode (128 bytes for rev0, inode_size bytes for rev1+) */
struct ext2_inode {
    u16 mode;
    u16 uid;
    u32 size_low;
    u32 atime;
    u32 ctime;
    u32 mtime;
    u32 dtime;
    u16 gid;
    u16 links_count;
    u32 blocks_count;       /* count in 512-byte units */
    u32 flags;
    u32 _os1;
    u32 block[15];          /* 12 direct + 1 single + 1 double + 1 triple */
    u32 generation;
    u32 file_acl;
    u32 size_high;          /* high 32 bits of file size (regular files, rev1+) */
    u32 faddr;
    u8  _os2[12];
};

/* Directory entry (variable length, name NOT null-terminated on disk) */
struct ext2_dirent {
    u32  inode;
    u16  rec_len;
    u8   name_len;
    u8   file_type;
    char name[255];
};

#pragma pack()

/* Register the ext2 filesystem type with VFS */
void ext2_init(void);
