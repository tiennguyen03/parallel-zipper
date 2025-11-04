# Parallel Text Compressor

A multithreaded text compression tool implemented in C using pthreads and zlib compression.

## Project Overview

This project implements a multithreaded version of a text compression tool that processes multiple `.txt` files concurrently. Each thread compresses one file independently using zlib, and results are stored in a shared array. The main thread then merges outputs in lexicographic order.

## Group Members

- Tien Nguyen (U62965888)
- James Huynh (U64017788)  
- Anthony Saade (U91712971)

## Performance Results

### Speedup Results:
- **books dataset**: 4.58× speedup (2.29s → 0.50s)
- **books2 dataset**: 4.32× speedup (1.60s → 0.37s)

### Compression Rates:
- 87.73% compression rate
- 89.38% compression rate

## Features

- **Multithreaded Processing**: Uses up to 19 concurrent threads for parallel file compression
- **Batch Processing**: Processes files in batches to manage thread resources efficiently
- **High Compression**: Uses zlib with maximum compression level (level 9)
- **Lexicographic Ordering**: Maintains consistent output order regardless of thread completion order
- **Large Buffer Support**: 1MB buffer size for efficient file I/O

## Technical Details

### Environment
- **Platform**: macOS (Apple M2)
- **Compiler**: gcc via clang
- **Threading**: POSIX pthreads
- **Compression**: zlib library

### Key Components

- **Task Structure**: Holds file information and processing index
- **Result Structure**: Stores compressed data and size
- **Thread Pool**: Manages concurrent compression tasks
- **Batch Processing**: Limits concurrent threads to prevent resource exhaustion

## Build Instructions

```bash
cd src
make
```

## Usage

```bash
./tzip <directory_name>
```

The program will:
1. Find all `.txt` files in the specified directory
2. Compress them using multiple threads
3. Output the compressed data to `text.tzip`
4. Display compression statistics

## Project Structure

```
src/
├── main.c          # Entry point
├── serial.c        # Main compression logic
├── serial.h        # Header file
├── Makefile        # Build configuration
├── check.sh        # Testing script
├── books/          # Test dataset 1
├── books2/         # Test dataset 2
└── text.tzip       # Compressed output
```

## Algorithm

1. **Directory Scanning**: Identify all `.txt` files in the target directory
2. **File Sorting**: Sort filenames lexicographically for consistent output
3. **Batch Processing**: Create threads in batches (max 19 concurrent)
4. **Parallel Compression**: Each thread compresses one file using zlib
5. **Result Collection**: Store compressed data in shared results array
6. **Output Generation**: Write all compressed data to single output file

## Memory Management

- Dynamic allocation for file lists and thread arrays
- Proper cleanup of all allocated resources
- Buffer reuse optimization for compression operations

## Thread Safety

- Each thread operates on independent file data
- Results stored in pre-allocated array slots
- No race conditions in shared data access

## License

This project is part of COP4600 coursework.
