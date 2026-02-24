#pragma once
#include "types.h"
#include "vfs.h"

/* Create a kernel pipe. Returns 0 on success with read/write ends in r/w.
   Both ends share a 4 KB ring buffer; reads block until data available. */
i32 pipe_create(struct vfs_file **r, struct vfs_file **w);
