#include "utils.h"

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

// fast hex printing for 16-bit words
void hex16(char *out, uint16_t v)
{
    const char hex_digits[] = "0123456789abcdef";

    out[0] = hex_digits[(v >> 12) & 0xF];
    out[1] = hex_digits[(v >> 8) & 0xF];
    out[2] = hex_digits[(v >> 4) & 0xF];
    out[3] = hex_digits[v & 0xF];
}
