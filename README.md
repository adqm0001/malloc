# cma: a Custom Memory Allocator

A naive implementation of `malloc`, `free`, `calloc`, and `realloc` in C. I built this because I wanted to actually understand what's happening under the hood instead of just treating `malloc` as a black box.

## How does malloc() actually work?

Under the hood, `malloc` asks the OS for a chunk of memory on the heap. If there's no suitable free block lying around already, it grabs a new one using `sbrk()`. `sbrk()` moves the "break" (the address marking the end of the heap) by a relative number of bytes, and returns where the break used to be, which is the start of your new memory. 

Example: say the break starts at `0x1000`. Call `sbrk(1024)` and you get back `0x1000` (the old break), while the break itself moves to `0x1400`. I used `sbrk()` instead of `brk()` here since it moves the break by a relative amount rather than needing an absolute address.

`sbrk()` is simple and fast, but it only grows the heap in one direction which causes fragmentation, for example if A gets allocated then B gets allocated and A gets freed, the break is still stuck behind B. 

For big allocations, `cma` uses `mmap()` instead. It grabs a separate region from the OS that can be returned independently with munmap(), so large blocks don't pin the heap or cause external fragmentation. The tradeoff is a syscall per allocation and rounding up to page size, so it's only worth it above a threshold.

## How does free() know how much to free?

It only gets a pointer, so it needs some way to know the size. The trick is to stash a small metadata header right before the pointer you hand back to the user. So if someone calls `malloc(256)` you don't just `sbrk(256)` you `sbrk(256 + META_SIZE)`, write the metadata at the start, and return the pointer just past it. When `free()` is called, it just steps back `META_SIZE` bytes to read that metadata and find out how big the block actually is.

The metadata struct looks like this:

```c
struct block_meta {
    size_t size;
    struct block_meta *prev;
    struct block_meta *next;
    bool free;
    bool is_mmap;
};
```
All the heap-allocated (non-mmap) blocks (both free and in use) live in a doubly linked list, `free_list`.

## Roughly what my implementation does

**malloc(size):** First decide whether to use `sbrk()` or `mmap()` based on a threshold I picked (inspired by glibc). If there's no free list yet, just grab fresh memory. If there is one, walk it looking for a free block big enough. The size get rounded up to a multiple of sizeof(void*) (8 bytes on 64-bit). I just do a first-fit linear search here (I could've done something smarter like best-fit, size-segretated free lists, and merging adjacent free blocks while searching, but this is a simple malloc, so I skipped that). Once I find a spot, I check if it's worth splitting: if the block has more room than the request plus another `META_SIZE` plus a bit extra (`sizeof(void*)`), I split it into two blocks and fix up the prev/next pointers so the list stays consistent.

**realloc(ptr, new_size):** If the user wants more space, I check if the previous and/or next blocks are free and whether combining them would cover the new size. If yes, I merge them (using `memmove` if the previous block is involved, since the data has to move to the start of the merged block). If the new size is actually smaller, I just try to split the block instead. And if none of that works, I fall back to plain `malloc` + `memcpy` + `free` on the old pointer (but only after confirming the new pointer is valid so the caller's original pointer is untouched if it fails).

**calloc(num, size):** First I make sure `num * size` won't overflow by checking `size > SIZE_MAX / num`, if it would, I return NULL instead of letting it wrap around into a tiny allocation. Otherwise it's just `malloc(num * size)` followed by `memset` to zero it out.

**free(ptr):** Check whether the block was allocated with `mmap` or `sbrk`. If `mmap`, just `munmap()` it. Otherwise, mark it free and try to coalesce it with its neighbors.

## What I'd improve

- Linear search through the free list is slow, size-segregated free lists or a tree keyed by size would help a lot on a fragmented heap
- No tail pointer, so appending to the list also means a full walk
- Could merge adjacent free blocks opportunistically while searching, not just on free/realloc
- Right now it's one free list for everything, could split it into multiple lists by size class (like glibc's bins) would probably help both speed and fragmentation

Project's done, but I really enjoyed working through this, it's naive and definitely not production-grade, but it works as a drop-in for the four standard functions.

