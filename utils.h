#ifndef __UTILS__H__
#define __UTILS__H__

#include "loop.h"
#include <linux/fs.h>     
#include <linux/types.h>  

// Resource release for FileContext
int32_t release_file_context(FileContext* ctx);
// Convert 16-bit value to hexadecimal string
void hex16(char *out, uint16_t v);
// Parse bytes into 16-bit words
void parse_words(uint16_t *curr_line, const uint8_t *kbuf, size_t size);
// Format and write a full line (offset + 8 words). Returns kernel_write result.
ssize_t write_line(FileContext* file_ctx,uint16_t *curr_line, size_t curr_size);
// Write '*' for repeated lines. Returns kernel_write result.
ssize_t write_repeated_line(FileContext* file_ctx);


#endif // __UTILS__H__
