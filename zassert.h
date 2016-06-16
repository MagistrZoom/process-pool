#pragma once
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define zassert(eq) if(eq){ printf("Err on line: %d\n", __LINE__); perror("ffu"); free_handler(-1); }
