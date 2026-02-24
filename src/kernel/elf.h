#pragma once
#include "types.h"

#define ELF_MAGIC 0x464C457F  // "\x7FELF"

// ELF class
#define ELFCLASS64 2

// ELF type
#define ET_EXEC 2

// ELF machine
#define EM_X86_64 62

// Program header types
#define PT_NULL    0
#define PT_LOAD    1

// Program header flags
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
    u32 e_magic;
    u8  e_class;
    u8  e_data;
    u8  e_hversion;
    u8  e_osabi;
    u64 e_pad;
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed)) Elf64_Phdr;
