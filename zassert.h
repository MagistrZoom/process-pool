#pragma once
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int perr(int err){                                
    char *err_ptr = strerror(err); 
    fprintf(stderr, "%s\n", err_ptr);     
    return err;                                   
}

#define zassert(eq) if(eq){ free_handler(-1); printf("Err on line: %d\n", __LINE__); exit(perr(errno)); }
