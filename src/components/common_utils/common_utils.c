#include "include/common_utils.h"
#include <errno.h>
#include <stdlib.h>

int safe_realloc(void **ptr, size_t size)
{
    void *tmp;
    if(size == 0) {
        return 0;
    }
    tmp = realloc(*ptr, size);
    if(tmp) {
        *ptr = MOVE(tmp);
        return 0;
    }
    return ENOMEM;
}
