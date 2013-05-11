#define _GNU_SOURCE
#include "brk.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h> 
#include <errno.h> 
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <stdbool.h>


#define NALLOC 1024                                     /* minimum #units to request */
#define BEST_FIT 2
#define FIRST_FIT 1

typedef long Align;                                     /* for alignment to long boundary */

union header {                                          /* block header */
  struct {
    union header *ptr;                                  /* next block if on free list */
    unsigned size;                                      /* size of this block  - what unit? */ 
  } s;
  Align x;                                              /* force alignment of blocks */
};

typedef union header Header;

static Header base;                                     /* empty list to get started */
static Header *freep = NULL;                            /* start of free list */

/* free: put block ap in the free list */

void free(void * ap)
{
  Header *bp, *p;

  if(ap == NULL) return;                                /* Nothing to do */

  bp = (Header *) ap - 1;                               /* point to block header */
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;                                            /* freed block at atrt or end of arena */

  if(bp + bp->s.size == p->s.ptr) {                     /* join to upper nb */
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  }
  else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp) {                             /* join to lower nbr */
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

/* morecore: ask system for more memory */

#ifdef MMAP

static void * __endHeap = 0;

void * endHeap(void)
{
  if(__endHeap == 0) __endHeap = sbrk(0);
  return __endHeap;
}
#endif


static Header *morecore(unsigned nu)
{
  void *cp;
  Header *up;
#ifdef MMAP
  unsigned noPages;
  if(__endHeap == 0) __endHeap = sbrk(0);
#endif

  if(nu < NALLOC)
    nu = NALLOC;
#ifdef MMAP
  noPages = ((nu*sizeof(Header))-1)/getpagesize() + 1;
  cp = mmap(__endHeap, noPages*getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  nu = (noPages*getpagesize())/sizeof(Header);
  __endHeap += noPages*getpagesize();
#else
  cp = sbrk(nu*sizeof(Header));
#endif
  if(cp == (void *) -1){                                 /* no space at all */
    perror("failed to get more memory");
    return NULL;
  }
  up = (Header *) cp;
  up->s.size = nu;
  free((void *)(up+1));
  return freep;
}
/*
 * malloc allocates a block of nbytes bytes of data 
 * and returns a pointer to the block.
 * Uses either best-fit och first-fit, depending on
 * the STRATEGY symbol
 *
 */
void * malloc(size_t nbytes)
{
  if (nbytes == 0 || nbytes == RLIMIT_DATA)
    return NULL;

  Header *p, *prevp;
  Header * morecore(unsigned);
  unsigned nunits;

  if(nbytes == 0) 
    return NULL;

  nunits = (nbytes+sizeof(Header)-1)/sizeof(Header) +1;

  if((prevp = freep) == NULL) {
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  #if STRATEGY == BEST_FIT
  Header * best_block = NULL;                           /* initialize best fit header here */
  #endif

  for(p= prevp->s.ptr;  ; prevp = p, p = p->s.ptr) {
    if(p->s.size >= nunits) {                           /* big enough */
    #if STRATEGY == FIRST_FIT
      if (p->s.size == nunits){                         /* exactly */
        prevp->s.ptr = p->s.ptr;
      } else {                                          /* allocate tail end */
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void *)(p+1);
    #elif STRATEGY == BEST_FIT
      if (p->s.size == nunits){                         /* fits exactly */
        prevp->s.ptr = p->s.ptr;
        freep = prevp;
        return (void *)(p+1);
      }
      if (best_block == NULL)
          best_block = p;
      else if (best_block->s.size > p->s.size)
          best_block = p;
    #endif
    }
    if(p == freep){                                     /* wrapped around free list */
    #if STRATEGY == FIRST_FIT
      if((p = morecore(nunits)) == NULL)
        return NULL;                                    /* none left */
    #elif STRATEGY == BEST_FIT
        if (best_block == NULL){                        /* no best fit block found */
          if((p = morecore(nunits)) == NULL)
            return NULL; 
        } else{                                         /* return tail end of best fit block */
          best_block->s.size -= nunits;
          best_block += best_block->s.size;
          best_block->s.size = nunits;
          freep = prevp;
          return (void *)(best_block+1);
        }
    #endif
    }
  }
}
 /*
  * realloc takes a pointer to a memory block and a size.
  * It allocates a block of memory of the given size
  * containing the data at the given pointer and returns it.
  */
void *realloc (void * ptr, size_t nbytes){
  if (ptr == NULL)
    return malloc (nbytes);

  /* allocate a new block of memory */
  void * cpy = malloc (nbytes);
  /* retrive the Header of the given block */
  Header * ptr_header = (Header *)ptr-1;

  /* If we're trying to enlarge a block, we want to copy
   * only the number of bytes present in the given block.
   * If we're decreasing the block size, we want to copy
   * as many as the new block can fit. 
   * 
   * The wanted value will therefore always be the smallest of
   * nbytes and the old block size.
   */

  /*find the size of the given block in bytes, minus the block header */
  unsigned cpy_size = ptr_header->s.size * sizeof(Header)- sizeof (Header);

  if (cpy_size > nbytes) /* true if we're decreasing the block, false if increasing */
    cpy_size = nbytes;

  /*copy the data */
  memcpy (cpy, ptr, cpy_size);
  /* free the old block */
  free (ptr);
  return cpy;
}




