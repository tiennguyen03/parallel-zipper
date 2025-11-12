/**
 
* COP4600 Project 2 – Multithreaded Text Compression
* Group Members:
* Tien Nguyen (U62965888)
* James Huynh (U64017788)
* Anthony Saade (U91712971)
*
* Description:
* Implements a multithreaded version of the text compression tool using pthreads.
* Each thread compresses one .txt file independently with zlib, storing results
* 8 in a shared results array. The main thread merges outputs in lexicographic order.
*
* Speedup Results:
* books: 4.58× (2.29s → 0.50s)
* books2: 4.32× (1.60s → 0.37s)
* Compression Rates: 87.73% and 89.38%
* Environment: macOS (Apple M2, gcc via clang), pthreads, zlib*/


#include <dirent.h> 
#include <stdio.h> 
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>

#define BUFFER_SIZE 1048576 // 1MB
#define MAX_THREADS 8

// Structure for file compression task
typedef struct {
    char *filename;
    char *directory_name;
    int index;
    unsigned char *compressed_data;
    int compressed_size;
    unsigned char *original_data;
    int original_size;
    int completed;
} file_task_t;

// Thread pool and synchronization
typedef struct {
    file_task_t *tasks;
    int total_files;
    int next_task;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    int shutdown;
    pthread_mutex_t completion_mutex;
} thread_pool_t;

int cmp(const void *a, const void *b) {
    return strcmp(*(char **) a, *(char **) b);
}

// Worker thread function - processes one file at a time
void* process_file(void *arg) {
    thread_pool_t *pool = (thread_pool_t*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Check for shutdown or available work
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }
        
        // Get next available task
        if (pool->next_task >= pool->total_files) {
            pthread_mutex_unlock(&pool->queue_mutex);
            usleep(1000); // Brief pause if no work
            continue;
        }
        
        int task_index = pool->next_task++;
        file_task_t *task = &pool->tasks[task_index];
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // Build full file path
        int path_len = strlen(task->directory_name) + strlen(task->filename) + 2;
        char *full_path = malloc(path_len * sizeof(char));
        assert(full_path != NULL);
        snprintf(full_path, path_len, "%s/%s", task->directory_name, task->filename);
        
        // Read file content
        FILE *f_in = fopen(full_path, "r");
        assert(f_in != NULL);
        
        task->original_data = malloc(BUFFER_SIZE);
        assert(task->original_data != NULL);
        
        int nbytes = fread(task->original_data, sizeof(unsigned char), BUFFER_SIZE, f_in);
        fclose(f_in);
        task->original_size = nbytes;
        free(full_path);
        
        // Compress the file data
        task->compressed_data = malloc(BUFFER_SIZE);
        assert(task->compressed_data != NULL);
        
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        
        int ret = deflateInit(&strm, 9);
        assert(ret == Z_OK);
        
        strm.avail_in = nbytes;
        strm.next_in = task->original_data;
        strm.avail_out = BUFFER_SIZE;
        strm.next_out = task->compressed_data;
        
        ret = deflate(&strm, Z_FINISH);
        assert(ret == Z_STREAM_END);
        
        task->compressed_size = BUFFER_SIZE - strm.avail_out;
        deflateEnd(&strm);
        
        // Mark task as completed
        pthread_mutex_lock(&pool->completion_mutex);
        task->completed = 1;
        pthread_mutex_unlock(&pool->completion_mutex);
    }
    
    return NULL;
}

int compress_directory(char *directory_name) {
    DIR *d;
    struct dirent *dir;
    char **files = NULL;
    int nfiles = 0;

    // Scan directory for text files (same as original)
    d = opendir(directory_name);
    if(d == NULL) {
        printf("An error has occurred\n");
        return 0;
    }

    while ((dir = readdir(d)) != NULL) {
        files = realloc(files, (nfiles+1)*sizeof(char *));
        assert(files != NULL);

        int len = strlen(dir->d_name);
        if(len >= 4 && 
           dir->d_name[len-4] == '.' && 
           dir->d_name[len-3] == 't' && 
           dir->d_name[len-2] == 'x' && 
           dir->d_name[len-1] == 't') {
            files[nfiles] = strdup(dir->d_name);
            assert(files[nfiles] != NULL);
            nfiles++;
        }
    }
    closedir(d);
    
    if (nfiles == 0) {
        printf("No text files found in directory\n");
        if (files) free(files);
        return 0;
    }
    
    qsort(files, nfiles, sizeof(char *), cmp);

    // Initialize thread pool
    thread_pool_t pool;
    pool.tasks = malloc(nfiles * sizeof(file_task_t));
    assert(pool.tasks != NULL);
    pool.total_files = nfiles;
    pool.next_task = 0;
    pool.shutdown = 0;
    
    pthread_mutex_init(&pool.queue_mutex, NULL);
    pthread_cond_init(&pool.queue_cond, NULL);
    pthread_mutex_init(&pool.completion_mutex, NULL);
    
    // Initialize tasks
    for(int i = 0; i < nfiles; i++) {
        pool.tasks[i].filename = files[i];
        pool.tasks[i].directory_name = directory_name;
        pool.tasks[i].index = i;
        pool.tasks[i].compressed_data = NULL;
        pool.tasks[i].compressed_size = 0;
        pool.tasks[i].original_data = NULL;
        pool.tasks[i].original_size = 0;
        pool.tasks[i].completed = 0;
    }
    
    // Create worker threads (optimized number based on file count)
    int num_threads = nfiles < MAX_THREADS ? nfiles : MAX_THREADS;
    if (num_threads < 1) num_threads = 1;
    
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    assert(threads != NULL);
    
    // Start worker threads
    for(int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, process_file, &pool);
    }
    
    // Open output file
    FILE *f_out = fopen("text.tzip", "w");
    assert(f_out != NULL);
    
    int total_in = 0, total_out = 0;
    
    // Write compressed files in correct order as they complete
    for(int i = 0; i < nfiles; i++) {
        // Wait for this specific task to complete
        while (1) {
            pthread_mutex_lock(&pool.completion_mutex);
            if (pool.tasks[i].completed) {
                pthread_mutex_unlock(&pool.completion_mutex);
                break;
            }
            pthread_mutex_unlock(&pool.completion_mutex);
            usleep(100); // Small pause to avoid busy waiting
        }
        
        // Write compressed data to output file
        file_task_t *task = &pool.tasks[i];
        fwrite(&task->compressed_size, sizeof(int), 1, f_out);
        fwrite(task->compressed_data, sizeof(unsigned char), task->compressed_size, f_out);
        
        total_in += task->original_size;
        total_out += task->compressed_size;
    }
    
    fclose(f_out);
    printf("Compression rate: %.2lf%%\n", 100.0*(total_in-total_out)/total_in);
    
    // Cleanup - signal threads to exit
    pool.shutdown = 1;
    
    for(int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Free all allocated memory
    for(int i = 0; i < nfiles; i++) {
        free(files[i]);
        if (pool.tasks[i].original_data) free(pool.tasks[i].original_data);
        if (pool.tasks[i].compressed_data) free(pool.tasks[i].compressed_data);
    }
    free(files);
    free(pool.tasks);
    free(threads);
    
    pthread_mutex_destroy(&pool.queue_mutex);
    pthread_cond_destroy(&pool.queue_cond);
    pthread_mutex_destroy(&pool.completion_mutex);

    return 0;
}
