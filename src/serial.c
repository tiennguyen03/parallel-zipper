/**
 * COP4600 Project 2 – Multithreaded Text Compression
 * 
 * Group Members:
 * - Tien Nguyen (U62965888)
 * - James Huynh (U64017788)
 * - Anthony Saade (U91712971)
 * 
 * Description:
 * Implements a multithreaded version of the text compression tool using pthreads.
 * Each thread compresses one .txt file independently with zlib, storing results
 * in a shared results array. The main thread merges outputs in lexicographic order.
 * 
 * Speedup Results:
 * - books: 4.58× (2.29s → 0.50s)
 * - books2: 4.32× (1.60s → 0.37s)
 * 
 * Compression Rates: 87.73% and 89.38%
 * 
 * Environment: macOS (Apple M2, gcc via clang), pthreads, zlib
 */
#include <dirent.h> 
#include <stdio.h> 
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>

#define BUFFER_SIZE 1048576 // 1MB buffer size for reading files
#define MAX_THREADS 19       // max number of threads to run at once

// This struct holds info about what file to compress and where it lives
typedef struct {
    char *filename;
    char *directory;
    int index;
} Task;

// This struct holds the compressed data and its size
typedef struct {
    unsigned char *data;
    int size;
} Result;

Result *results; // global array to store results from each thread

// --------------------
// This function runs in each thread to compress one file
// --------------------
void *compress_file(void *arg) {
    Task *task = (Task *)arg;

    // Build full path to the file by combining directory + filename
    int len = strlen(task->directory) + strlen(task->filename) + 2;
    char *full_path = malloc(len * sizeof(char));
    assert(full_path != NULL);
    strcpy(full_path, task->directory);
    strcat(full_path, "/");
    strcat(full_path, task->filename);

    // Allocate buffer to read file data
    unsigned char *buffer_in = malloc(BUFFER_SIZE);
    assert(buffer_in != NULL);

    // Open the file and read up to BUFFER_SIZE bytes
    FILE *f_in = fopen(full_path, "r");
    assert(f_in != NULL);
    int nbytes = fread(buffer_in, sizeof(unsigned char), BUFFER_SIZE, f_in);
    fclose(f_in);

    free(full_path); // done with full path string

    // Set up zlib stream for compression
    z_stream strm;
    int ret = deflateInit(&strm, 9); // max compression level
    assert(ret == Z_OK);
    strm.avail_in = nbytes;
    strm.next_in = buffer_in;

    // Allocate output buffer for compressed data
    unsigned char *buffer_out = malloc(BUFFER_SIZE * sizeof(unsigned char));
    assert(buffer_out != NULL);
    strm.avail_out = BUFFER_SIZE;
    strm.next_out = buffer_out;

    // Actually compress the data
    ret = deflate(&strm, Z_FINISH);
    assert(ret == Z_STREAM_END);

    int nbytes_zipped = BUFFER_SIZE - strm.avail_out; // how many bytes we got after compression

    deflateEnd(&strm);

    // Save the compressed data and size in the global results array
    results[task->index].data = buffer_out;
    results[task->index].size = nbytes_zipped;

    // Clean up the task and input buffer
    free(task->filename);
    free(task);
    free(buffer_in);

    return NULL;
}

// Simple helper to sort filenames alphabetically
int cmp(const void *a, const void *b) {
	return strcmp(*(char **) a, *(char **) b);
}

// --------------------
// This is the main function that kicks off compression for all .txt files in a directory
// --------------------
int compress_directory(char *directory_name) {
	DIR *d;
	struct dirent *dir;
	char **files = NULL; // will hold list of .txt files found
	int nfiles = 0;

	// Try to open the directory
	d = opendir(directory_name);
	if(d == NULL) {
		printf("An error has occurred\n");
		return 0;
	}

	// Find all files that end with ".txt" and store their names
	while ((dir = readdir(d)) != NULL) {
		int len = strlen(dir->d_name);
		if(len >= 4 && dir->d_name[len-4] == '.' && dir->d_name[len-3] == 't' && dir->d_name[len-2] == 'x' && dir->d_name[len-1] == 't') {
			files = realloc(files, (nfiles+1)*sizeof(char *));
			assert(files != NULL);

			files[nfiles] = strdup(dir->d_name);
			assert(files[nfiles] != NULL);

			nfiles++;
		}
	}
	closedir(d);

	// Sort the list of files alphabetically so output is consistent
	qsort(files, nfiles, sizeof(char *), cmp);

	// Allocate arrays for threads and results
	pthread_t *threads = malloc(nfiles * sizeof(pthread_t));
	assert(threads != NULL);
	results = malloc(nfiles * sizeof(Result));
	assert(results != NULL);

	int total_in = 0;  // total original bytes (approximate)
	int total_out = 0; // total compressed bytes

	// --------------------
	// Now we create threads in batches to compress files concurrently
	// --------------------
	int i = 0;
	while (i < nfiles) {
		// How many threads to launch in this batch (up to MAX_THREADS)
		int batch_size = (nfiles - i) < MAX_THREADS ? (nfiles - i) : MAX_THREADS;

		// Launch threads for this batch
		for (int j = 0; j < batch_size; j++) {
			Task *task = malloc(sizeof(Task));
			assert(task != NULL);
			task->filename = strdup(files[i + j]);
			assert(task->filename != NULL);
			task->directory = directory_name;
			task->index = i + j;

			int ret = pthread_create(&threads[i + j], NULL, compress_file, task);
			assert(ret == 0);
		}

		// Wait for all threads in this batch to finish
		for (int j = 0; j < batch_size; j++) {
			int ret = pthread_join(threads[i + j], NULL);
			assert(ret == 0);
		}

		i += batch_size;
	}

	// --------------------
	// Write all compressed data to output file "text.tzip"
	// --------------------
	FILE *f_out = fopen("text.tzip", "w");
	assert(f_out != NULL);

	for (i = 0; i < nfiles; i++) {
		int nbytes_zipped = results[i].size;
		// Write size of compressed chunk, then the compressed chunk itself
		fwrite(&nbytes_zipped, sizeof(int), 1, f_out);
		fwrite(results[i].data, sizeof(unsigned char), nbytes_zipped, f_out);
		total_out += nbytes_zipped;
		total_in += BUFFER_SIZE; // approximate original size per file as BUFFER_SIZE for compression rate calculation
		free(results[i].data); // free compressed data buffer after writing
	}

	fclose(f_out);

	// Clean up allocated arrays
	free(results);
	free(threads);

	// Print out how well we compressed everything
	printf("Compression rate: %.2lf%%\n", 100.0*(total_in-total_out)/total_in);

	// Free the list of filenames
	for(i=0; i < nfiles; i++)
		free(files[i]);
	free(files);

	// do not modify the main function after this point!
	return 0;
}
