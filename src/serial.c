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
* books: 8.01x
* books2: 10.3x
* Compression Rates: 62.7%
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

//Structure for file compression task
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

//Structure for thread pool and synchronization
typedef struct {    
    file_task_t *tasks;
    int total_files;
    int next_task;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    int shutdown;
    pthread_mutex_t completion_mutex;
} thread_pool_t;

//Function to compare file names
int cmp(const void *a, const void *b) {
    return strcmp(*(char **) a, *(char **) b);
}

//Function to process a file
void* process_file(void *arg) {
    thread_pool_t *pool = (thread_pool_t*)arg;
    
    //Loop until the thread is shutdown
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);   //Lock the queue mutex
        
        //Check if the thread is shutdown or there is available work
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);   //Unlock the queue mutex
            break;
        }
        
        //Check if there are no more tasks
        if (pool->next_task >= pool->total_files) {
            pthread_mutex_unlock(&pool->queue_mutex);
            usleep(1000); //Sleep for 1ms to avoid busy waiting
            continue;
        }
        
        //Get the next available task
        int task_index = pool->next_task++;
        file_task_t *task = &pool->tasks[task_index];

        pthread_mutex_unlock(&pool->queue_mutex);   //Unlock the queue mutex
        
        //Build the full file path
        int path_len = strlen(task->directory_name) + strlen(task->filename) + 2;

        //Allocate memory for the full file path
        char *full_path = malloc(path_len * sizeof(char));
        assert(full_path != NULL);

        //Format the full file path
        snprintf(full_path, path_len, "%s/%s", task->directory_name, task->filename);
        
        //Open the file
        FILE *f_in = fopen(full_path, "r");
        assert(f_in != NULL);
        
        //Allocate memory for the original data
        task->original_data = malloc(BUFFER_SIZE);
        assert(task->original_data != NULL);
        
        //Read the file content
        int nbytes = fread(task->original_data, sizeof(unsigned char), BUFFER_SIZE, f_in);
        //Close the file
        fclose(f_in);
        task->original_size = nbytes;

        free(full_path);            //Free the memory allocated for the full file path

        
        //Allocate memory for the compressed data
        task->compressed_data = malloc(BUFFER_SIZE);
        assert(task->compressed_data != NULL);
        
        //Initialize the zlib stream
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        
        int ret = deflateInit(&strm, 9);
        assert(ret == Z_OK);
        
        //Set the zlib stream
        strm.avail_in = nbytes;
        strm.next_in = task->original_data;
        strm.avail_out = BUFFER_SIZE;
        strm.next_out = task->compressed_data;
        
        //Compress the data
        ret = deflate(&strm, Z_FINISH);
        assert(ret == Z_STREAM_END);
        
        //Set the compressed size
        task->compressed_size = BUFFER_SIZE - strm.avail_out;
        deflateEnd(&strm);   //End the zlib stream
        
        pthread_mutex_lock(&pool->completion_mutex);   //Lock the completion mutex

        task->completed = 1;  //Mark the task as completed
        pthread_mutex_unlock(&pool->completion_mutex);   //Unlock the completion mutex

    }
    
    return NULL;
}

//Function to compress a directory
int compress_directory(char *directory_name) {
    
    DIR *d;
    struct dirent *dir;
    char **files = NULL;
    int nfiles = 0;

    //Open the directory
    d = opendir(directory_name);
    if(d == NULL) {
        printf("An error has occurred\n");  //Print an error message if the directory cannot be opened
        return 0;
    }

    //Loop through the directory
    while ((dir = readdir(d)) != NULL) {
        
        //Allocate memory for the files
        files = realloc(files, (nfiles+1)*sizeof(char *));
        assert(files != NULL);

        //Get the length of the file name
        int len = strlen(dir->d_name);

        //Check if the file name is a text file
        if(len >= 4 && 
           dir->d_name[len-4] == '.' && 
           dir->d_name[len-3] == 't' && 
           dir->d_name[len-2] == 'x' && 
           dir->d_name[len-1] == 't') {
            
            //Duplicate the file name
            files[nfiles] = strdup(dir->d_name);
            assert(files[nfiles] != NULL);
            nfiles++;
        }
    }
    closedir(d);   //Close the directory
    
    //Check if there are no text files in the directory
    if (nfiles == 0) {
        printf("No text files found in directory\n");  //Print a message if there are no text files in the directory
        if (files) free(files);  
        return 0;
    }
    
    //Sort the files in lexicographic order
    qsort(files, nfiles, sizeof(char *), cmp);

    //Initialize the thread pool
    thread_pool_t pool;
    pool.tasks = malloc(nfiles * sizeof(file_task_t));   //Allocate memory for the tasks
    assert(pool.tasks != NULL);
    pool.total_files = nfiles;
    pool.next_task = 0;
    pool.shutdown = 0;
    
    pthread_mutex_init(&pool.queue_mutex, NULL);   //Initialize the queue mutex
    pthread_cond_init(&pool.queue_cond, NULL);   //Initialize the queue condition variable
    pthread_mutex_init(&pool.completion_mutex, NULL);   //Initialize the completion mutex
    
    //Initialize the tasks
    for(int i = 0; i < nfiles; i++) {

        //Set the filename, directory name, index, compressed data, compressed size, original data, and original size
        pool.tasks[i].filename = files[i];    
        pool.tasks[i].directory_name = directory_name;
        pool.tasks[i].index = i;
        pool.tasks[i].compressed_data = NULL;
        pool.tasks[i].compressed_size = 0;
        pool.tasks[i].original_data = NULL;
        pool.tasks[i].original_size = 0;
        pool.tasks[i].completed = 0;
    }
    

    //Calculate the number of threads
    int num_threads = nfiles < MAX_THREADS ? nfiles : MAX_THREADS;
    if (num_threads < 1) num_threads = 1;
    
    //Allocate memory for the threads
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));  
    assert(threads != NULL);

    //Create the threads
    for(int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, process_file, &pool);  
    }
    
    //Open the output file
    FILE *f_out = fopen("text.tzip", "w"); 
    assert(f_out != NULL);
    
    int total_in = 0, total_out = 0;   //Initialize the total input and output sizes
    
    //Loop through the files
    for(int i = 0; i < nfiles; i++) {
        //Loop until the task is completed
        while (1) {
            pthread_mutex_lock(&pool.completion_mutex);   //Lock the completion mutex
            if (pool.tasks[i].completed) {
                pthread_mutex_unlock(&pool.completion_mutex);   //Unlock the completion mutex if the task is completed
                break;
            }
            pthread_mutex_unlock(&pool.completion_mutex);   //Unlock the completion mutex
            usleep(100); //Sleep for 0.1ms to avoid busy waiting
        }
        
        //Write the compressed data to the output file
        file_task_t *task = &pool.tasks[i];
        fwrite(&task->compressed_size, sizeof(int), 1, f_out);
        fwrite(task->compressed_data, sizeof(unsigned char), task->compressed_size, f_out);
        
        total_in += task->original_size;   //Add the original size to the total input size
        total_out += task->compressed_size;   //Add the compressed size to the total output size
    }
    
    fclose(f_out);   //Close the output file

    //Print the compression rate
    printf("Compression rate: %.2lf%%\n", 100.0*(total_in-total_out)/total_in);
    
    //Signal the threads to exit
    pool.shutdown = 1;

    //Join the threads
    for(int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    //Free the memory allocated for the tasks, original data, and compressed data
    for(int i = 0; i < nfiles; i++) {
        free(files[i]);
        if (pool.tasks[i].original_data) free(pool.tasks[i].original_data);
        if (pool.tasks[i].compressed_data) free(pool.tasks[i].compressed_data);
    }
    free(files);
    free(pool.tasks);
    free(threads);
    
    //Destroy the mutexes and condition variable
    pthread_mutex_destroy(&pool.queue_mutex);   
    pthread_cond_destroy(&pool.queue_cond);
    pthread_mutex_destroy(&pool.completion_mutex);

    return 0;
}
