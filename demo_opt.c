#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include "xmalloc.h"

int
main(int argc, char* argv[])
{   
    long** longs = malloc(100 * sizeof(long*));
    for (int i = 0; i < 100; i++) {
        longs[i] = xmalloc(90);
        *longs[i] = i;
     }

    for (int i = 0; i < 100; i++) {
        printf("here is %d = %ld\n", i, *longs[i]);
        xfree(longs[i]);
    }

    //long* large = xmalloc(66000);
    //large[0] = 12;
    //large[8000] = 321;

    //printf("large[0] = %ld large[8000] = %ld\n", large[0], large[8000]);
    //`xfree(large); 

    //printf("43 = %ld\n", *longs[43]);
    //long* n = xmalloc(8);
    //*n = 2;
    //printf("n is %p\n", (void*)n);
    //xfree(n);

    //long* n1 = xmalloc(8);
    //long* n2 = xmalloc(8);

    //*n1 = 23;
    //*n2 = 232;

    //printf("n1 = %ld, n2 = %ld\n", *n1, *n2);

    //xfree(n1);
    //xfree(n2);

    return 0;
}
