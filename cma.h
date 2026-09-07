#ifndef CMA_H
#define CMA_H

#include <stddef.h>
#include <stdbool.h>

typedef struct block_meta {
  size_t size;
  struct block_meta *prev;
  struct block_meta *next;
  bool free;
  bool is_mmap;
} block_meta;

#define META_SIZE sizeof(block_meta)
#define MMAP_THRESHOLD (128 * 1024)

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t num, size_t size);
void *realloc(void *ptr, size_t size);

#endif
