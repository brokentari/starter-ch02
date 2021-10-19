#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include "xmalloc.h"
#include "list.h"

int
main(int argc, char* argv[])
{
    long* n = xmalloc(400);
    *n = 123;
    printf("before realloc %ld\n", *n);

    long* newN = xrealloc(n ,600);
    printf("same long %ld\n", *newN);
    return 0;
}

