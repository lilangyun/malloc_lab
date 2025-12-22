/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * 显式空闲链表 + 分离适配 + LIFO
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
static size_t get_index(size_t size);  //确认放入哪个链表

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* 链表的数量 */
#define LIST_NUM 15

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define MAX(x, y) ( (x) < (y) ? (y) : (x))
#define MIN(x, y) ( (x) < (y) ? (x) : (y))

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)  //块：堆拓展的最小单位

#define PACK(size, alloc) ((size) | (alloc))
#define PACK_PLUS(size, prealloc, alloc) ((size) | (prealloc) | (alloc))  //设置prev_alloc和alloc

#define GET(p) (*(unsigned int*)(p))
#define PUT(p, val) (*(unsigned int*)(p)) = (val)

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) (GET(p) & 0x2)

#define SET_ALLOC(p) (GET(p) |= 0x1)
#define SET_PREV_ALLOC(p) (GET(p) |= 0x2)  //将prev_alloc置为1
#define SET_PRE_FREE(p) (GET(p) &= ~0x2)

#define HDPT(bp) ((char *)(bp) - WSIZE)  //Header Pointor
#define FTPT(bp) ((char *)(bp) + GET_SIZE(HDPT(bp)) - DSIZE)  //Footer Pointor

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))  //根据header确定位置
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))  //根据footer确定位置

#define NEXT_NODE(bp) ((char *)mem_heap_lo() + *((int *)(bp)) )
#define PREV_NODE(bp) ((char *)mem_heap_lo() + *((unsigned int*)((char *)(bp) + WSIZE)) )
#define SET_NEXT_NODE(bp, val) ((*(unsigned int*)(bp)) = ((unsigned)(long)val))
#define SET_PREV_NODE(bp, val) ((*((unsigned int*)((char *)(bp) + WSIZE))) = ((unsigned)(long)val))

static char *heap_listp;  //堆的第一个有效载荷
static long **free_list;  //分离链表数组

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // 空闲链表初始化
    free_list = mem_heap_lo();
    int i;
    for(i = 0; i<LIST_NUM; i++){
        if((heap_listp = mem_sbrk(DSIZE)) == (void *)(-1)){
            return -1;
        }
        free_list[i] = mem_heap_lo();  //表示当前链表为空
    }

    // 分配其他内存
    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)(-1)){
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + 1*WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 2*WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 3*WSIZE, PACK(0, 1));
    // 第一个有效载荷位置
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
    size_t asize;  //表示adjusted size

    if(size == 0){
        return NULL;
    }

    asize = ALIGN(size + WSIZE);  //还需要考虑header的大小

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
void mm_free(void *ptr)
{
    if(ptr == NULL){
        return ;
    }
    size_t size = GET_SIZE(HDPT(ptr));
    size_t prev_alloc = GET_PREV_ALLOC(HDPT(ptr));

    PUT(HDPT(ptr), PACK_PLUS(size, prev_alloc, 0));
    PUT(FTPT(ptr), PACK_PLUS(size, prev_alloc, 0));

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

    size_t prev_alloc = GET_PREV_ALLOC(HDPT(bp));

    PUT(HDPT(bp), PACK_PLUS(size, prev_alloc, 0));  //write old epilogue
    PUT(FTPT(bp), PACK_PLUS(size, prev_alloc, 0));
    PUT(HDPT(NEXT_BLKP(bp)), PACK(0,1));  //epilogue header

    // 合并空闲块
    return coalesce(bp);
}

// 合并空闲块
static void *coalesce(void *bp){
    if(bp == NULL){
        return NULL;
    }
    size_t prev_alloc = GET_PREV_ALLOC(HDPT(bp));
    size_t next_alloc = GET_ALLOC(HDPT(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDPT(bp));

    if(prev_alloc && next_alloc){
        SET_PREV_ALLOC(bp);
    }
    else if(prev_alloc && (!next_alloc)){
        // 删除next_block结点
        delete_node(NEXT_BLKP(bp));

        size += GET_SIZE(HDPT(NEXT_BLKP(bp)));
        PUT(HDPT(bp), PACK_PLUS(size, 2, 0));
        PUT(FTPT(bp), PACK_PLUS(size, 2, 0));
    }
    else if((!prev_alloc) && next_alloc){
        // 删除prev_block结点
        delete_node(PREV_BLKP(bp));

        SET_PRE_FREE(HDPT(NEXT_BLKP(bp)));

        size += GET_SIZE(HDPT(PREV_BLKP(bp)));
        size_t prev_alloc = GET_PREV_ALLOC(HDPT(PREV_BLKP(bp)));
        PUT(HDPT(PREV_BLKP(bp)), PACK_PLUS(size, prev_alloc, 0));
        PUT(FTPT(bp), PACK_PLUS(size, prev_alloc,0));
        bp = PREV_BLKP(bp);
    }
    else{
        // 删除prev + next结点
        delete_node(PREV_BLKP(bp));
        delete_node(NEXT_BLKP(bp));

        size += GET_SIZE(HDPT(PREV_BLKP(bp))) + GET_SIZE(FTPT(NEXT_BLKP(bp)));
        size_t prev_alloc = GET_PREV_ALLOC(HDPT(PREV_BLKP(bp)));
        PUT(HDPT(PREV_BLKP(bp)), PACK_PLUS(size, prev_alloc, 0));
        PUT(FTPT(NEXT_BLKP(bp)), PACK_PLUS(size, prev_alloc, 0));
        bp = PREV_BLKP(bp);
    }

    // 插入结点
    insert_node(bp);

    return bp;
}

// 首次适配算法
static void *find_fit(size_t asize){
    void *bp;
    size_t l_size;  // list_size

    int i;
    for(i = 0; i < LIST_NUM; i++){
        for(bp = free_list[i]; bp != mem_heap_lo(); bp = NEXT_NODE(bp)){
            l_size = GET_SIZE(HDPT(bp));
            if(l_size >= asize){
                return bp;
            }
        }
    }

    return NULL;
}

// 放置空闲块
static void place(void *bp, size_t asize){
    size_t old_size = GET_SIZE(HDPT(bp));

    // 删除结点
    delete_node(bp);

    if(old_size - asize >= 2*DSIZE){
        // 分割空闲块
        PUT(HDPT(bp), PACK_PLUS(old_size - asize, GET_PREV_ALLOC(HDPT(bp)), 1));
        
        bp = NEXT_BLKP(bp);
        PUT(HDPT(bp), PACK_PLUS(old_size - asize, 2, 0));
        PUT(FTPT(bp), PACK_PLUS(old_size - asize, 2, 0));

        // 添加结点
        insert_node(bp);
    }
    else{
        // 具有内部碎片
        SET_ALLOC(HDPT(bp));

        // 设置next_block的prev已分配
        SET_PREV_ALLOC(HDPT(NEXT_BLKP(bp)));
        // 如果next_block未分配，则设置其footor
        if(!GET_ALLOC(HDPT(NEXT_BLKP(bp)))){
            SET_PREV_ALLOC(FTPT(NEXT_BLKP(bp)));
        }
    }
}

// 测试用例的malloc最大数字不超过2^15 = 32768，因此可分15个大小类隐式空闲链表
static size_t get_index(size_t size) {
    if (size <= 24)
        return 0;
    if (size <= 32)
        return 1;
    if (size <= 64)
        return 2;
    if (size <= 80)
        return 3;
    if (size <= 120)
        return 4;
    if (size <= 240)
        return 5;
    if (size <= 480)
        return 6;
    if (size <= 960)
        return 7;
    if (size <= 1920)
        return 8;
    if (size <= 3840)
        return 9;
    if (size <= 7680)
        return 10;
    if (size <= 15360)
        return 11;
    if (size <= 30720)
        return 12;
    if (size <= 61440)
        return 13;
    else
        return 14;
}

void insert_node(void *bp){
    size_t idx = get_index(GET_SIZE(HDPT(bp)));

    // 如果所在大小类链表中有free结点
    if(free_list[idx] != mem_heap_lo()){
        SET_NEXT_NODE(bp, free_list[idx]);
        SET_PREV_NODE(bp, mem_heap_lo());
        SET_PREV_NODE(free_list[idx], bp);
    }
    else{
        SET_NEXT_NODE(bp, mem_heap_lo());
        SET_PREV_NODE(bp, mem_heap_lo());
    }
    free_list[idx] = bp;
}

void delete_node(void *bp){
    size_t idx = get_index(GET_SIZE(HDPT(bp)));
    char *prev = PREV_NODE(bp);
    char *next = NEXT_NODE(bp);

    // bp是头结点
    if(prev == mem_heap_lo()){
        SET_NEXT_NODE(free_list[idx], next);
        if(next != mem_heap_lo()){
            SET_PREV_NODE(next, mem_heap_lo());
        }
    }
    else{
        SET_NEXT_NODE(prev, next);
        if(next != mem_heap_lo()){
            SET_PREV_NODE(next, prev);
        }
    }
}