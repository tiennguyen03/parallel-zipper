#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <stddef.h>

// Each thread compresses one file
typedef struct {
    char *path;
    int index;
} task_t;

// Result after compression
typedef struct {
    unsigned char *data;
    size_t size;
    int status;
} result_t;

// Called by main.c — do not rename
void compress_directory(char *directory_name);

#endif