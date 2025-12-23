/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * explicit free list --> segrated free list
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

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

static void insert_node(void *bp);
static void delete_node(void *bp);

static size_t get_idex(size_t asize);  //获取asize对应的list的idx

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define LIST_NUM 15

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define MAX(x, y) ( (x) < (y) ? (y) : (x))
#define MIN(x, y) ( (x) < (y) ? (x) : (y))

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)  //块：堆拓展的最小单位

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int*)(p))
#define PUT(p, val) (*(unsigned int*)(p)) = (val)  // 改变bp所指向内存的值

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDPT(bp) ((char *)(bp) - WSIZE)  //Header Pointor
#define FTPT(bp) ((char *)(bp) + GET_SIZE(HDPT(bp)) - DSIZE)  //Footer Pointor

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

#define PREV_NDPT(bp)  (*(unsigned int*)(bp))  //next node pointer
#define NEXT_NDPT(bp)  (*((unsigned int*)(bp) + 1))  //next node pointer

#define SET_PREV_NDPT(bp, val) ((*(unsigned int*)(bp)) = (val))
#define SET_NEXT_NDPT(bp, val) ((*((unsigned int*)(bp) + 1)) = (val))


static char *heap_listp;
static char *list_start;  // lists基址

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // 优先为lists分配list_num个存储位置
    if((heap_listp = mem_sbrk(LIST_NUM*WSIZE)) == (void *)(-1)){
        return -1;
    }
    for(int i=0; i<LIST_NUM; i++){
        PUT(heap_listp + i*WSIZE, 0);
    }
    list_start = heap_listp;


    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)(-1)){
        return -1;
    }
    PUT(heap_listp, 0);
    PUT(heap_listp + 1*WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 2*WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 3*WSIZE, PACK(0, 1));
    heap_listp += 2*WSIZE;

    // 堆拓展
    if((extend_heap(CHUNKSIZE/WSIZE)) == NULL){
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
    size_t asize;  // adjusted size

    if(size == 0){
        return NULL;
    }

    asize = ALIGN(size + DSIZE);

    if((bp = find_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }
    
    //当前chunk找不到可供使用的空闲链表，新分配一个chunk
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize/WSIZE)) == NULL){
        return NULL;
    }

    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    if(bp == NULL){
        return ;
    }
    size_t size = GET_SIZE(HDPT(bp));

    PUT(HDPT(bp), PACK(size, 0));
    PUT(FTPT(bp), PACK(size, 0));
    SET_PREV_NDPT(bp, 0);
    SET_NEXT_NDPT(bp, 0);

    // 合并空闲块
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    if(size == 0){
        mm_free(ptr);
    }

    newptr = mm_malloc(size);
    if(newptr == NULL){
        return NULL;  //没有分配成功
    }

    if(oldptr == NULL){
        return newptr;
    }

    copySize = GET_SIZE(HDPT(oldptr)) - DSIZE;  //有效载荷大小
    if (size < copySize){
        copySize = size;
    }
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

    PUT(HDPT(bp), PACK(size,0));  //write old epilogue
    PUT(FTPT(bp), PACK(size,0));
    SET_PREV_NDPT(bp, 0);
    SET_NEXT_NDPT(bp, 0);

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

    }
    else if(prev_alloc && (!next_alloc)){

        delete_node(NEXT_BLKP(bp));

        size += GET_SIZE(HDPT(NEXT_BLKP(bp)));
        PUT(HDPT(bp), PACK(size, 0));
        PUT(FTPT(bp), PACK(size, 0));
        SET_PREV_NDPT(bp, 0);
        SET_NEXT_NDPT(bp, 0);
    }
    else if((!prev_alloc) && next_alloc){

        delete_node(PREV_BLKP(bp));
        
        size += GET_SIZE(HDPT(PREV_BLKP(bp)));
        PUT(FTPT(bp), PACK(size, 0));
        PUT(HDPT(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        SET_PREV_NDPT(bp, 0);
        SET_NEXT_NDPT(bp, 0);
    }
    else{
        size += GET_SIZE(HDPT(PREV_BLKP(bp))) + GET_SIZE(FTPT(NEXT_BLKP(bp)));

        delete_node(PREV_BLKP(bp));
        delete_node(NEXT_BLKP(bp));

        PUT(HDPT(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTPT(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        SET_PREV_NDPT(bp, 0);
        SET_NEXT_NDPT(bp, 0);
    }

    insert_node(bp);
    return bp;
}

// 找到合适的空闲块
static void *find_fit(size_t asize){
    size_t psize;
    void *tmp;  // 临时变量

    size_t idex = get_idex(asize);
    void *bp = list_start + idex*WSIZE;

    for(; bp != list_start + LIST_NUM*WSIZE; bp += WSIZE){
        tmp = GET(bp);
        while(tmp != NULL){
            psize = GET_SIZE(HDPT(tmp));
            if(psize >= asize){
                return tmp;
            }
            tmp = NEXT_NDPT(tmp);
        }
    }

    return NULL;
}

// 放置空闲块
static void place(void *bp, size_t asize){
    size_t old_size = GET_SIZE(HDPT(bp));

    // 在空闲链表中删除结点
    delete_node(bp);

    if(old_size - asize >= 2*DSIZE){
        // 分割空闲块
        PUT(HDPT(bp), PACK(asize, 1));
        PUT(FTPT(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDPT(bp), PACK(old_size - asize, 0));
        PUT(FTPT(bp), PACK(old_size - asize, 0));
        SET_PREV_NDPT(bp, 0);
        SET_NEXT_NDPT(bp, 0);

        // 在free_list中添加新结点
        // insert_node(bp);
        coalesce(bp);
    }
    else{
        // 内部碎片
        PUT(HDPT(bp), PACK(old_size, 1));  //注意这里是old_size，有可能比size大
        PUT(FTPT(bp), PACK(old_size, 1));  //只不过没有分割的必要
    }
}

static void insert_node(void *bp){
    if(bp == NULL){
        return ;  // 安全检查
    }

    // 获取idex
    size_t idex = get_idex(GET_SIZE(HDPT(bp)));
    void *list_head = list_start + idex*WSIZE;
    void *first_node = GET(list_head);  // 指向首元素的指针

    if(first_node == NULL){
        PUT(list_head, bp);
    }
    else{
        SET_NEXT_NDPT(bp, first_node);
        SET_PREV_NDPT(first_node, bp);
        PUT(list_head, bp);
    }
}

static void delete_node(void *bp){
    if((bp == NULL) || GET_ALLOC(HDPT(bp))){
        return ;  // 安全检查
    }

    // 获取idex
    size_t idex = get_idex(GET_SIZE(HDPT(bp)));
    void *list_head = list_start + idex*WSIZE;  // 指向lists[idex]的指针

    void *prev = PREV_NDPT(bp);
    void *next = NEXT_NDPT(bp);

    SET_PREV_NDPT(bp, 0);
    SET_NEXT_NDPT(bp, 0);

    if((prev == NULL) && (next == NULL)){
        PUT(list_head, 0);
    }
    else if(prev == NULL){
        SET_PREV_NDPT(next, prev);
        PUT(list_head, next);
    }
    else if(next == NULL){
        SET_NEXT_NDPT(prev, 0);
    }
    else{
        SET_NEXT_NDPT(prev, next);
        SET_PREV_NDPT(next, prev);
    }
}

static size_t get_idex(size_t asize){
    if (asize <= 24)
        return 0;
    if (asize <= 32)
        return 1;
    if (asize <= 64)
        return 2;
    if (asize <= 80)
        return 3;
    if (asize <= 120)
        return 4;
    if (asize <= 240)
        return 5;
    if (asize <= 480)
        return 6;
    if (asize <= 960)
        return 7;
    if (asize <= 1920)
        return 8;
    if (asize <= 3840)
        return 9;
    if (asize <= 7680)
        return 10;
    if (asize <= 15360)
        return 11;
    if (asize <= 30720)
        return 12;
    if (asize <= 61440)
        return 13;
    else
        return 14;
}