/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "TJU",
    /* First member's full name */
    "Liyixuan",
    /* First member's email address */
    "lilangyun@tju.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)(-1)){
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + 1*WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 2*WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 3*WSIZE, PACK(0, 1));
    heap_listp += 2*WSIZE;

    // 堆拓展
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL){
        return -1;
    }
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    char *bp;
    size_t extendsize;  //堆需要拓展的字节

    if(size == 0){
        return NULL;
    }

    size = ALIGN(size + DSIZE);

    if((bp = find_fit(size)) != NULL){
        place(bp, size);
        return bp;
    }

    //当前chunk找不到可供使用的空闲链表，新分配一个chunk
    extendsize = MAX(size, CHUNKSIZE);
    if((bp = extend_heap(extendsize/WSIZE)) == NULL){
        return NULL;
    }

    place(bp, size);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    if(ptr == NULL){
        return ;
    }
    size_t size = GET_SIZE(HDPT(ptr));

    PUT(HDPT(ptr), PACK(size, 0));
    PUT(FTPT(ptr), PACK(size, 0));

    // 合并空闲块
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    size = GET_SIZE(oldptr);
    copySize = GET_SIZE(HDPT(newptr));
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

// 拓展堆
static void *extend_heap(size_t words){
    char *bp;
    size_t size;
    
    size = (words % 2) ? ((words+1) * WSIZE) : (words * WSIZE);
    if ((long)(bp = mem_sbrk(size)) == -1){
        return NULL;
    }

    PUT(HDPT(bp), PACK(size,0));
    PUT(FTPT(bp), PACK(size,0));
    PUT(HDPT(NEXT_BLKP(bp)), PACK(0,1));  //epilogue header

    // 合并空闲块
    return coalesce(bp);
}

// 合并空闲块
static void *coalesce(void *bp){
    if(bp == NULL){
        return NULL;
    }
    size_t prev_alloc = GET_ALLOC(FTPT(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDPT(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDPT(bp));

    if(prev_alloc && next_alloc){
        return bp;
    }
    else if(prev_alloc && (!next_alloc)){
        size += GET_SIZE(HDPT(NEXT_BLKP(bp)));
        PUT(HDPT(bp), PACK(size, 0));
        PUT(FTPT(bp), PACK(size, 0));
    }
    else if((!prev_alloc) && next_alloc){
        size += GET_SIZE(HDPT(PREV_BLKP(bp)));
        PUT(FTPT(bp), PACK(size, 0));
        PUT(HDPT(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else{
        size += GET_SIZE(HDPT(PREV_BLKP(bp))) + GET_SIZE(FTPT(NEXT_BLKP(bp)));
        PUT(HDPT(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTPT(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

// 寻找适配空闲块
static void *find_fit(size_t size){
    void *bp = heap_listp;

    // 确保不是结束块epilogue，在分配堆范围之内
    while( GET_SIZE(HDPT(bp)) > 0 ){
        if( (!GET_ALLOC(HDPT(bp))) && (GET_SIZE(HDPT(bp)) >= size) ){
            return bp;
        }
        bp = NEXT_BLKP(bp);
    }
    return NULL;
}

// 放置空闲块
static void place(void *bp, size_t size){
    size_t old_size = GET_SIZE(HDPT(bp));

    PUT(HDPT(bp), PACK(size, 1));
    PUT(FTPT(bp), PACK(size, 1));

    if(old_size - size >= 2*DSIZE){
        // 分割空闲块
        bp = NEXT_BLKP(bp);
        PUT(HDPT(bp), PACK(old_size - size, 0));
        PUT(FTPT(bp), PACK(old_size - size, 0));
    }
}