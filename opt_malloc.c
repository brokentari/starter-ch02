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
    int arena;
    struct bucket_node *prev;
    struct bucket_node *next;
    // depends on size. start with all zeros.
    unsigned char *bitmap;
    // data goes here
} bucket_node;

// all the size buckets we will allow.
static int bucket_sizes[10] = {4, 8, 16, 32, 64, 128, 256, 512, 1024};

// have the buckets been set up yet?
static char arenas_initialized = 0;

//multiple arenas
static bucket_node *arenas[8][9];

static __thread int favorite_arena_index = 0;

// initialization mutex
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

// one mutex per arenas
static pthread_mutex_t arena_mutexes[8] = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };


// create buckets
void init_arenas()
{
	for (int arena = 0; arena < 8; arena++) 
	{
    		for (int i = 0; i < 9; i++)
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
			    bucket->arena = arena;
       			bucket->prev = 0; //TODO: fix this later
        		int bitmap_size = (((4096 - 32) / bucket_size) / 8) + 1;

        		memset((void *)&bucket->bitmap, 0, bitmap_size);

        		arenas[arena][i] = bucket;
    		}
	}

	favorite_arena_index = pthread_self() % 8;
}

// get the index of the bucket that contains items of size 'size'
size_t get_bucket_size_index(size_t size)
{
    for (int i = 0; i < 9; i++)
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

    for (int i = 0; i < 8; i++)
    {
        if (size > bucket_sizes[i] && size <= bucket_sizes[i + 1])
        {
            return bucket_sizes[i + 1];
        }
    }

    return size;
}
 

void visualize_bitmap(bucket_node* bucket, size_t size, size_t cpos, size_t bpos) {
    int bitmap_size = (((4096 - 32) / size) / 8) + 1;
    //int available_items_space = ((4096 - 24) - bitmap_size) / size;

    printf("========================\nBitmap for bucket %p of size %zu\n", bucket, bucket->size);
    // loop over size of bitmap. keep track of items looked at
    // becuase bitmap may be larger than actual num items in page.
    int items_checked = 0;
    for (int i = 0; i < bitmap_size; i++)
    {  
        unsigned char p = *(unsigned char *)((void *)&bucket->bitmap + i);
        printf("char at %p %d #%d\n", (unsigned char *)((void *)&bucket->bitmap + i), p, i);
        // inner loop to look at each bit in each byte
        for (int j = 0; j < 8; j++)
        {
            if (cpos == i && bpos+1==j)
            {
                printf("==========================\n\n");
                return;
            }

           // unsigned char bitwise_op = (unsigned int) 1 << bit_pos;
            

            //int possible_open_spot = p & bitwise_op;
            //printf("is %dth pos open? %d\n", j, possible_open_spot);
            

            items_checked++;
        }
    }

    printf("\n\n");
}

bucket_node *add_page(size_t size, bucket_node *og_head)
{
    int bucket_index = get_bucket_size_index(size);

    bucket_node *new_bucket = mmap(
        0,
        4096,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );

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
    new_bucket->size = size;
    new_bucket->arena = og_head->arena;
    new_bucket->prev = 0; 
    int bitmap_size = (((4096 - 32) / size) / 8) + 1;

    memset((void *)&new_bucket->bitmap, 0, bitmap_size);

    arenas[og_head->arena][bucket_index] = new_bucket;
    return new_bucket;
}

void* search_bitmap(size_t bitmap_size, size_t available_items_space, bucket_node* bucket, size_t size) { 
   
    // loop over size of bitmap. keep track of items looked at
    // becuase bitmap may be larger than actual num items in page.
    //visualize_bitmap(bucket, size);
    int items_checked = 0;
    //printf("bitmapsize %zu\n", bitmap_size);
    for (int i = 0; i < bitmap_size; i++)
    {
        if (items_checked >= available_items_space)
        {
            break;
        }    

        unsigned char p = *(unsigned char *)((void *)&bucket->bitmap + i);
        if (p == 255) {
            items_checked += 8;
            continue;
        }

        unsigned char flip_p = ~p;

        int bit_pos =  ffs((long) flip_p) - 1;

        items_checked += bit_pos + 1;
        if (items_checked >= available_items_space) {
            break;
        }

        unsigned char bitwise_op = (unsigned int) 1 << bit_pos;

        int bytes_offset = ((i * 8) + bit_pos) * size;

        //set spot we return to 1
        *(unsigned char *)((void *)&bucket->bitmap + i) = p | bitwise_op;

        //return bucket mem location offset by size of  and
        //bytes offset based on free location
        return ((void *)bucket + 32 + bitmap_size + bytes_offset);
    }
    return 0;
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
    // lenght of bitmap in bytes
    int bitmap_size = (((4096 - 32) / size) / 8) + 1;
    int available_items_space = ((4096 - 32) - bitmap_size) / size;

    void* return_ptr = search_bitmap(bitmap_size, available_items_space, bucket, size);
    if (return_ptr != 0) {
        return return_ptr;
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
void *find_open_mem(size_t size, long a_idx)
{
    int bucket_index = get_bucket_size_index(size);
    bucket_node **selected_arena = arenas[a_idx];
    bucket_node *bucket = selected_arena[bucket_index];
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
    size_t num_pages = div_up(bytes + 32, 4096);
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
    bucket->arena = -1;
    // ignore bitmap entirely 
    return ((void*) bucket + 32);
} 

void *
xmalloc(size_t bytes)
{
    // initialize buckets
    if (arenas_initialized == 0)
    {
	pthread_mutex_lock(&init_mutex);

	if (arenas_initialized == 0)
	{
		init_arenas();
		arenas_initialized = 1;
	}
	
	pthread_mutex_unlock(&init_mutex);
    }

    // find the correct bucket size
    int dest_bucket = div_up_bucket(bytes);
    

    // if the allocation size is less than our "large" size, go
    // into the buckets
    if (dest_bucket <= 1024)
    { 
        int arena_index = favorite_arena_index;

	int rv;
	rv = pthread_mutex_trylock(&arena_mutexes[arena_index]);

	while (rv)
	{
		arena_index = (arena_index + 1) % 8;
		rv = pthread_mutex_trylock(&arena_mutexes[arena_index]);	
	}

            
        // go into the buckets and look for an available block of memory
        void *open_spot = find_open_mem(dest_bucket, arena_index);
      
        pthread_mutex_unlock(&arena_mutexes[arena_index]);
        return open_spot;
    }
    // if the allocation is greater than 4096, we might just
    // need to mmap and return the address
    else
    {   
        void* return_ptr = large_alloc(bytes);
        return return_ptr;
       
    }

    return 0;
}

void xfree(void *ptr)
{
    bucket_node* bucket = (void*)(4096 * ((uintptr_t)ptr / (uintptr_t)4096));

    if (bucket->size > 1024) {
        // with large alloc, just munmap
        munmap((void*) bucket, bucket->size);
    }
    else {
        pthread_mutex_lock(&arena_mutexes[bucket->arena]);
   
        int bitmap_size = (((4096 - 32) / bucket->size) / 8) + 1;

        int bytes_offset = (void*)ptr - (void*) bucket;
       // printf("offset for free %d\n", bytes_offset);
        int bitmap_and_offset = bytes_offset - 32 - bitmap_size;
        int index_in_bitmap = bitmap_and_offset / bucket->size;

        int bit_pos = index_in_bitmap % 8;
        unsigned char bitwise_op = 1 << bit_pos;
 
        // set right spot in bitmap to 0
        int char_pos = index_in_bitmap / 8;
        //printf("attempting free on bucket %p of size %ld, charpos=%d bit=%d\n", (void*)bucket, bucket->size, char_pos, bit_pos);
        unsigned char p = *(unsigned char*)((void*)&bucket->bitmap + char_pos);

        //visualize_bitmap(bucket, bucket->size, char_pos, bit_pos);
        *(unsigned char*)((void*)&bucket->bitmap + char_pos) = p ^ bitwise_op;
        //visualize_bitmap(bucket, bucket->size, char_pos, bit_pos);


        pthread_mutex_unlock(&arena_mutexes[bucket->arena]);
    }
}

void *
xrealloc(void *prev, size_t bytes)
{
    void* new_ptr = xmalloc(bytes);
    bucket_node* bucket = (void*)(4096 * ((uintptr_t)prev / (uintptr_t)4096));
    
    if (bucket->size <= 1024) {
        pthread_mutex_lock(&arena_mutexes[bucket->arena]); 
    }

    memcpy(new_ptr, prev, bucket->size);

    if (bucket->size <= 1024) {
        pthread_mutex_unlock(&arena_mutexes[bucket->arena]);
    }
    
    xfree(prev);
    return new_ptr;
}

