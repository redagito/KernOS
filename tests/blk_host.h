#pragma once

#include <kernel/block.h>

/* Host test backend: a stdio-backed block_dev over a raw image file.
   The 'r+b' handle keeps fixture modifications possible; gone on
   process exit, so errors leave no residue in the repo tree. */
struct block_dev *blk_host_open(const char *path);
