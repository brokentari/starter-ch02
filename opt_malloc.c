
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include "xmalloc.h"

// bucket
typedef struct bucket_node
{
    size_t size;
    struct bucket_node *prev;
    struct bucket_node *next;
    // depends on size. start with all zeros.
    unsigned char *bitmap;
    // data goes here
} bucket_node;

// all the size buckets we will allow.
static int bucket_sizes[20] = {4, 8, 12, 16, 24, 42, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3192};

// have the buckets been set up yet?
static char buckets_initialized = 0;

// array of pointers to each bucket sizes linked list
static bucket_node *buckets[19];

// one mutex per bucket
//static pthread_mutex_t bucket_mutex_list[19] = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };

// create buckets
void init_buckets()
{
    for (int i = 0; i < 19; i++)
    {
        int bucket_size = bucket_sizes[i];

        bucket_node *bucket = mmap(
            0,
            4096,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);

        bucket->size = bucket_size;
        bucket->next = bucket;
        bucket->prev = 0; //TODO: fix this later
        int bitmap_size = (((4096 - 24) / bucket_size) / 8) + 1;

        memset((void *)&bucket->bitmap, 0, bitmap_size);

        //printf("ii %d\n", bucket_size);
        //printf("length %d\n", len_bitmap);

        buckets[i] = bucket;
    }
}

// get the index of the bucket that contains items of size 'size'
size_t get_bucket_size_index(size_t size)
{
    for (int i = 0; i < 19; i++)
    {
        if (size == bucket_sizes[i])
        {
            return i;
        }
    }

    return -1;
}

// return the bucket size we need
size_t div_up_bucket(size_t size)
{
    if (size < 4) {
        return 4;
    }

    for (int i = 0; i < 18; i++)
    {
        if (size > bucket_sizes[i] && size <= bucket_sizes[i + 1])
        {
            return bucket_sizes[i + 1];
        }
    }

    return size;
}

unsigned char get_bitwise_op(size_t i)
{
    int op = pow((double)2, (double)(7 - i));
    return op;
}

bucket_node *add_page(size_t size, bucket_node *og_head)
{

    //printf("adding page...\n");
    int bucket_index = get_bucket_size_index(size);

    bucket_node *new_bucket = mmap(
        0,
        4096,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);

    // this can be optimized by using the prev field.
    // instead of looping through it, we just the the original
    // "head" and get the previous to find the "end" of the list
    bucket_node *current = og_head;
    while (current->next != og_head)
    {
        current = current->next;
    }

    // when we break, current is "end" of list
    current->next = new_bucket;

    // insert new bucket into list at FRONT
    new_bucket->next = og_head;
    buckets[bucket_index] = new_bucket;
    new_bucket->size = size;
    new_bucket->prev = 0; //TODO: fix this later
    int bitmap_size = (((4096 - 24) / size) / 8) + 1;

    memset((void *)&new_bucket->bitmap, 0, bitmap_size);

    return new_bucket;
}

/*
*  find_mem_helper 
*  --------------------
*   finds an available block of space within a bucket contains blocks of certain size
    
    bucket: the bucket where we are looking for free space

    size: the size of the block of memory we are looking for

    og_head: the bucket that was passed in the very first call of find_mem_helper (remains the same
    throughout calls)
*/
void *find_mem_helper(bucket_node *bucket, size_t size, bucket_node *og_head)
{
    //printf("bucket mem = %p, size %ld\n", bucket, bucket->size);

    // lenght of bitmap in bytes
    int bitmap_size = (((4096 - 24) / size) / 8) + 1;
    int available_items_space = ((4096 - 24) - bitmap_size) / size;
    //printf("nums items %d, size of block %ld\n", available_items_space, size);

    // loop over size of bitmap. keep track of items looked at
    // becuase bitmap may be larger than actual num items in page.
    int items_checked = 0;
    for (int i = 0; i < bitmap_size; i++)
    {
        // inner loop to look at each bit in each byte
        for (int j = 0; j < 8; j++)
        {
            if (items_checked == available_items_space)
            {
                //printf("no more space\n");
                break;
            }

            unsigned char bitwise_op = get_bitwise_op(j);
            unsigned char p = *(unsigned char *)((void *)&bucket->bitmap + i);

            int possible_open_spot = p & bitwise_op;
            if (possible_open_spot == 0)
            {
                int bytes_offset = ((i * 8) + j) * size;

                // set spot we return to 1
                *(unsigned char *)((void *)&bucket->bitmap + i) = p | bitwise_op;

                // return bucket mem location offset by size of  and
                // bytes offset based on free location
                return ((void *)bucket + 24 + bitmap_size + bytes_offset);
            }

            items_checked++;
        }
    }

    // since we finished the loop above, which should return something if the
    // page contains space, we are here because that page is full and we
    // now need to create a new page
    if (bucket->next != og_head)
    {
        return find_mem_helper(bucket->next, size, og_head);
    }

    else
    {
        // we need a new page here.
        bucket_node *new_bucket = add_page(size, og_head);
        return find_mem_helper(new_bucket, size, og_head);
    }
}

// find an open memory spot in a bucket's bitmap
// if a space does not exist, return null ptr
void *find_open_mem(size_t size)
{
    int bucket_index = get_bucket_size_index(size);
    bucket_node *bucket = buckets[bucket_index];
    return find_mem_helper(bucket, size, bucket);
}

static size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx)
    {
        return zz;
    }
    else
    {
        return zz + 1;
    }
}

static void* large_alloc(size_t bytes)
{
    size_t num_pages = div_up(bytes, 4096);
    size_t total_size = num_pages * 4096;
    //printf("div up %zu,  alloc %zu \n", num_pages, total_size); 
    struct bucket_node* bucket = mmap(
        0,
        total_size,
        PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS,
        -1,
        0
    );

    bucket->size = total_size;
    bucket->next = 0;
    bucket->prev = 0; //TODO: fix this later
    // ignore bitmap entirely 
    return ((void*) bucket + 24);
} 


void *
xmalloc(size_t bytes)
{

    // initialize buckets
    if (buckets_initialized == 0)
    {
        init_buckets();
        buckets_initialized = 1;
    }

    // find the correct bucket size
    int dest_bucket = div_up_bucket(bytes);
    //printf("dest_bucket %d \n", dest_bucket);

    // if the allocation size is less than our "large" size, go
    // into the buckets
    if (dest_bucket < 3192)
    {
        // go into the buckets and look for an available block of memory
        void *open_spot = find_open_mem(dest_bucket);
        return open_spot;
    }
    // if the allocation is greater than 4096, we might just
    // need to mmap and return the address
    else
    {
        return large_alloc(bytes);
    }

    return 0;
}

void xfree(void *ptr)
{
    bucket_node* bucket = (void*)(4096 * ((uintptr_t)ptr / (uintptr_t)4096));
    //printf("bucket loc %p\n", (void*)bucket);
    if (bucket->size > 4096) {
        // with large alloc, just munmap
        //printf("freeing size %ld\n", bucket->size);
        munmap((void*) bucket, bucket->size);
    }
    else {
        int bitmap_size = (((4096 - 24) / bucket->size) / 8) + 1;

        int bytes_offset = (void*)ptr - (void*) bucket;
        bytes_offset = bytes_offset - bitmap_size;
        int index_in_bitmap = bytes_offset / bucket->size;

        int bit_pos = index_in_bitmap % 8;
        unsigned char bitwise_op = get_bitwise_op(bit_pos);
        //unsigned char bitwise_op = (int) pow(2, (8 - bit_pos));
 
        // set right spot in bitmap to 0
        int char_pos = index_in_bitmap / 8;
        unsigned char p = *(unsigned char*)((void*)&bucket->bitmap + char_pos);
        //printf("bitmap spot before %d\n", p);
        *(unsigned char*)((void*)&bucket->bitmap + char_pos) = p ^ bitwise_op;

        //printf("bitmap spot after %d\n", *(unsigned char*)((void*)&bucket->bitmap + char_pos));
    }
}

void *
xrealloc(void *prev, size_t bytes)
{
    // TODO: write an optimized realloc
    return 0;
}
