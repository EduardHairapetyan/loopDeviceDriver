#include "utils.h"
#include <linux/types.h>    
#include <linux/fs.h>       
#include <linux/string.h>   
#include <linux/uaccess.h>  
#include <linux/kernel.h>   
#include <linux/slab.h>     
#include <linux/errno.h>    
#include <linux/printk.h>   

// Release resources associated with the FileContext
int32_t release_file_context(FileContext* ctx)
{
    if (!ctx)
        return -1;

    if (ctx->file) {
        filp_close(ctx->file, NULL);
        ctx->file = NULL;
    }
    ctx->user_offset = 0;
    ctx->local_offset = 0;
    memset(ctx->prev_line,0,sizeof(ctx->prev_line));
    ctx->is_prev_line_identical = false;
    ctx->is_first_line = true;

    return 0;
}

// Parse bytes into 16-bit words
void parse_words(uint16_t *curr_line, const uint8_t *kbuf, size_t size)
{
    // Initialize curr_line to zero
    memset(curr_line, 0, 8 * sizeof(uint16_t));
    
    // Convert each pair of bytes in kbuf into a 16-bit word
    for (size_t byte_idx = 0; byte_idx < size; byte_idx += 2) {
        uint8_t low_byte = kbuf[byte_idx];
        uint8_t high_byte = (byte_idx + 1 < size) ? kbuf[byte_idx + 1] : 0;
        curr_line[byte_idx / 2] = low_byte | (high_byte << 8);
    }
}


// Format and write a full line (offset + 8 words). Returns kernel_write result. 
ssize_t write_line(FileContext* file_ctx,uint16_t *curr_line, size_t curr_size)
{
    char linebuf[128];
    int pos = 0;

    // offset 7 digits hex padded
    pos += scnprintf(linebuf + pos, sizeof(linebuf) - pos,
                     "%07zx ", (size_t)file_ctx->user_offset);

    // write words
    for (int i = 0; i < 8; i++) {
        if (i < (int)(curr_size / 2)) {
            hex16(linebuf + pos, curr_line[i]);
            pos += 4;
        } else {
            // Fill empty words with spaces
            memset(linebuf + pos, ' ', 4);
            pos += 4;
        }

        // Space between words (except after last)
        if (i != 7)
            linebuf[pos++] = ' ';
    }

    linebuf[pos++] = '\n';

    // Write the formatted line to the file
    return kernel_write(file_ctx->file, linebuf, pos, &(file_ctx->local_offset));
}

// Write the '*' repeated-line marker if needed. Returns kernel_write result or 0 if already written.
ssize_t write_repeated_line(FileContext* file_ctx)
{
    // Only write '*' if we haven't already for this repeated line
    if (!file_ctx->is_prev_line_identical) {
        ssize_t w = kernel_write(file_ctx->file, "*\n", 2, &(file_ctx->local_offset));
        if (w >= 0)
            file_ctx->is_prev_line_identical = true;
        return w;
    }
    return 0;
}

// fast hex printing for 16-bit words
void hex16(char *out, uint16_t v)
{
    const char hex_digits[] = "0123456789abcdef";

    // Convert each nibble to its hex character
    out[0] = hex_digits[(v >> 12) & 0xF];
    out[1] = hex_digits[(v >> 8) & 0xF];
    out[2] = hex_digits[(v >> 4) & 0xF];
    out[3] = hex_digits[v & 0xF];
}
