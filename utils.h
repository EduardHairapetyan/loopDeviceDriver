#ifndef __UTILS__H__
#define __UTILS__H__

#include "loop.h"
#include <linux/fs.h>     
#include <linux/types.h>  

int32_t release_file_context(FileContext* ctx);
void hex16(char *out, uint16_t v);
void parse_words(uint16_t *curr_line, const uint8_t *kbuf, size_t size);
ssize_t write_line(FileContext* file_ctx,uint16_t *curr_line, size_t curr_size);
ssize_t write_repeated_line(FileContext* file_ctx);


#endif // __UTILS__H__
