#ifndef __LOOP_H__
#define __LOOP_H__

#include <linux/fs.h>     
#include <linux/types.h>  

#define DEVICE_NAME     "loop"
#define TMP_FILE_PATH   "/tmp/output"
#define MINOR_NUM       0
// Maximum chunk size size for read/write operations
// Can be adjusted as needed
#define MAX_CHUNK_SIZE      65536

// File context structure
typedef struct FileContext {
    struct file * file;
    loff_t user_offset;
    loff_t local_offset;
    uint16_t prev_line[8];
    bool is_prev_line_identical;
    bool is_first_line;
} FileContext;

#endif // __LOOP_H__