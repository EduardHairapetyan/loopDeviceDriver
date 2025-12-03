#ifndef __UTILS__H__
#define __UTILS__H__

#include "loop.h"
#include <linux/fs.h>     
#include <linux/types.h>  

int32_t release_file_context(FileContext* ctx);
void hex16(char *out, uint16_t v);


#endif // __UTILS__H__
