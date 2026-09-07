#include "cma.h"
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static block_meta *free_list = NULL;

static block_meta *request_space(block_meta *head, size_t size){
  if (!size) return NULL;
  block_meta *ptr = (block_meta *)sbrk(size + META_SIZE);
  if (ptr == (void*)-1) {
    return NULL;
  }
  ptr->size = size;
  ptr->free = false;
  ptr->is_mmap = false;
  ptr->next = NULL;
  if (head) {
    head->next = ptr;
  }
  return ptr;
}

static block_meta *find_free_block(block_meta **last, size_t size){
  block_meta *current = free_list;
  while (current != NULL && !(current->free && current->size >= size)){
    *last = current;
    current = current->next;
  }
  return current;
}

static void split_block(struct block_meta *block, size_t size) {
  if (block->size >= size + META_SIZE + sizeof(void *)) {
    block_meta *new_block = (block_meta *)((char *)(block + 1) + size);
    new_block->size = block->size - size - META_SIZE;
    new_block->next = block->next;
    new_block->free = true;
    new_block->is_mmap = false;
    block->size = size;
    block->next = new_block;
  }
}

void *malloc(size_t size){
  if (size == 0) {
    return NULL;
  }

  block_meta *block;

  // Rounds up and then rounds down size to a multiple of 8
  size = (size + sizeof(void *) - 1) & ~(sizeof(void*) - 1);

  // If the size requested is more than our threshold we use mmap() to allocate 
  if (size >= MMAP_THRESHOLD) {
    void *ptr = mmap(NULL, size + META_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (ptr == MAP_FAILED) {
      perror("Malloc failed");
      return NULL;
    }

    block = (struct block_meta *)ptr;

    block->size = size;
    block->next = NULL;
    // Not tracked in the linked list since not continious with the heap or other mmap() blocks therefore no benefit to tracking it in a list
    block->free = false;
    block->is_mmap = true;
  } else {
    if (!free_list) {
      block = request_space(NULL, size);
      if (!block) {
        return NULL;
      }
      free_list = block;
    } else {
      block_meta *head = free_list;
      block = find_free_block(&head, size);
      if (!block) {
        block = request_space(head, size);
        if (!block) {
          return NULL;
        }
      } else {
        split_block(block, size);
        block->free = false;
      }
    }
  }
  return (block + 1);
}

static void coalesce_memory(block_meta *block){
  if (block != free_list) {
    block_meta *prev = free_list;
    while (prev != NULL && prev->next != block) {
      prev = prev->next;
    }
    if (prev && prev->free) {
       prev->size = prev->size + block->size + META_SIZE;
       prev->next = block->next;
       block = prev;
    }
  }

  block_meta *next = block->next;
  if (next && next->free) {
    block->size = block->size + next->size + META_SIZE;
    block->next = next->next;
  }

  return;
}

void free(void *ptr){
  if (!ptr) {
    return;
  }

  block_meta *block = ((block_meta *)ptr) - 1;
  
  if (block->is_mmap) {
    munmap(block, block->size + META_SIZE);
  } else {
    block->free = true; 
    coalesce_memory(block);
  }
}

void *calloc(size_t num, size_t size) {
  if (num != 0 && size > SIZE_MAX / num) {
    return NULL;
  }

  void *ptr = malloc(num * size);
  if (ptr == NULL) {
    return NULL;
  }

  memset(ptr, 0, num * size);
  return ptr;
}

void *realloc(void *ptr, size_t size) {
  if (ptr == NULL) { 
    return malloc(size);
  }

  if (!size) {
    free(ptr);
    return NULL;
  } 
  
  block_meta *block = ((block_meta *)ptr) - 1;
  size = (size + sizeof(void*) - 1) & ~(sizeof(void *) - 1);

  if (!block->is_mmap && block->size >= size){
    split_block(block, size);
    return ptr;
  }

   if (!block->is_mmap && block->next && block->next->free && 
       block->size + META_SIZE + block->next->size >= size) {
      block->size += META_SIZE + block->next->size;
      block->next = block->next->next;
      split_block(block, size);
      return ptr;
   }

   void *new_ptr = malloc(size);
   if (!new_ptr) {
    return NULL;
   }

   memcpy(new_ptr, ptr, block->size);
   free(ptr);
   return new_ptr;
}
