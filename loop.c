#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>


#define DEVICE_NAME     "loop"
#define TMP_FILE_PATH   "/tmp/output"
#define MINOR_NUM       0
// Maximum chunk size size for read/write operations
// Can be adjusted as needed
#define MAX_CHUNK_SIZE      65536

typedef struct FileContext {
    // Global kernel-space offset
    struct file * file;
    loff_t g_koffset;
    loff_t g_uoffset;
    uint16_t prev_line[8];
    bool prev_identical;
    bool first_line;
} FileContext;

// Major number for the device
static int major;
// Device class pointer
static struct class *loop_class;
// File context
static FileContext file_ctx = {
    .file = NULL,
    .g_koffset = 0,
    .g_uoffset = 0,
    .prev_line = {0},
    .prev_identical = false,
    .first_line = true,
};

// Helper function to set the devnode permissions
static char *set_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;  // Allow read and write access to all users
    return NULL;
}

// Open the device file
static int dev_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "loop device opened\n");

    int user_access_mode = file->f_flags & O_ACCMODE;
    int open_flags = user_access_mode;

    if (user_access_mode != O_RDONLY) {
        open_flags |= O_CREAT;
        if (file->f_flags & O_TRUNC)  open_flags |= O_TRUNC;
        if (file->f_flags & O_APPEND) open_flags |= O_APPEND;
    }
    open_flags |= O_LARGEFILE;

    printk(KERN_INFO "Opening file %s with flags 0x%x\n", TMP_FILE_PATH, open_flags);

    struct file *tmp_file = filp_open(TMP_FILE_PATH, open_flags, 0644);
    if (IS_ERR(tmp_file)) {
        printk(KERN_ERR "Failed to open file %s\n", TMP_FILE_PATH);
        return PTR_ERR(tmp_file);
    }

    file_ctx.file = tmp_file;
    return 0;
}

// Release the device file
static int dev_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "loop device released\n");

    if (file_ctx.file) {
        char linebuf[64];
        // Print final hex offset line
        int flen = scnprintf(linebuf, sizeof(linebuf), "%07zx\n", (size_t)file_ctx.g_koffset);
        kernel_write(file_ctx.file, linebuf, flen, &(file_ctx.g_uoffset));
        filp_close(file_ctx.file, NULL);
        file_ctx.file = NULL;
    }

    // Reset context 
    file_ctx.g_koffset = 0;
    file_ctx.g_uoffset = 0;
    memset(file_ctx.prev_line,0,sizeof(file_ctx.prev_line));
    file_ctx.prev_identical = false;
    file_ctx.first_line = true;

    printk(KERN_INFO "loop device closed\n");
    return 0;
}

// fast hex printing for 16-bit words
static void hex16(char *out, uint16_t v)
{
    const char hex_digits[] = "0123456789abcdef";

    out[0] = hex_digits[(v >> 12) & 0xF];
    out[1] = hex_digits[(v >> 8) & 0xF];
    out[2] = hex_digits[(v >> 4) & 0xF];
    out[3] = hex_digits[v & 0xF];
}

static ssize_t dev_write(struct file *file, const char __user *buf,
                         size_t len, loff_t *offset)
{
    if (!file_ctx.file)
        return -EIO;

    ssize_t ret = 0;
    size_t total_written = 0;
    size_t hex_offset = file_ctx.g_koffset;

    size_t chunkSize = min(len, MAX_CHUNK_SIZE);
    uint8_t* chunk = kmalloc(chunkSize, GFP_KERNEL);
    
    if (!chunk)
        return -ENOMEM;    

    // Read from user buffer in chunks
    while (total_written < len) {
        chunkSize = min(len - total_written, MAX_CHUNK_SIZE);

        if (copy_from_user(chunk, buf + total_written, chunkSize))
        {
            kfree(chunk);
            return total_written ? total_written : -EFAULT;
        }

        // now from small buffer read 16 byte chunks
        uint8_t kbuf[16] = {0};

        // process 16-byte chunks
        for (size_t i = 0; i < chunkSize; i += 16) {
            uint8_t curr_chunk_size = min((size_t)16, chunkSize - i);
            memcpy(kbuf, chunk + i, curr_chunk_size);
            char linebuf[64];       // final formatted output per line

            // parse bytes → uint16_t words
            uint16_t curr_line[8] = {0};
            for (size_t i = 0; i < curr_chunk_size; i += 2) {
                curr_line[i/2] = kbuf[i] | ((i+1 < curr_chunk_size ? kbuf[i+1] : 0) << 8);
            }

            // detect identical repeated line
            if (!file_ctx.first_line && memcmp(curr_line, file_ctx.prev_line, sizeof(curr_line)) == 0) {

                if (!file_ctx.prev_identical) {
                    // first time we see repeated line → output "*"
                    ret = kernel_write(file_ctx.file, "*\n", 2, offset);
                    if (ret < 0) {
                        kfree(chunk);
                        return total_written ? total_written : ret;
                    }
                    file_ctx.prev_identical = true;
                }
            } else {

                // build line: "00001fc0 1234 5678 ...\n"
                int pos = 0;

                // offset 7 digits hex padded
                pos += scnprintf(linebuf + pos, sizeof(linebuf) - pos, "%07zx ", hex_offset);

                // words
                for (int i = 0; i < 8; i++) {
                    if (i < curr_chunk_size/2) {
                        hex16(linebuf + pos, curr_line[i]);
                        pos += 4;
                    } else {

                        
                        if( i != curr_chunk_size/2)
                            linebuf[pos++] = ' ';
                        
                        linebuf[pos++] = ' ';
                        linebuf[pos++] = ' ';
                        linebuf[pos++] = ' ';
                        linebuf[pos++] = ' ';
                        continue;
                    }

                    if (i < 7)
                        linebuf[pos++] = ' ';
                }

                linebuf[pos++] = '\n';

                ret = kernel_write(file_ctx.file, linebuf, pos, offset);
                if (ret < 0) {
                    kfree(chunk);
                    return total_written ? total_written : ret;
                }

                memcpy(file_ctx.prev_line, curr_line, sizeof(curr_line));
                file_ctx.prev_identical = false;
            }
        

            file_ctx.first_line = false;
            total_written += curr_chunk_size;
            hex_offset += curr_chunk_size;
        }
    }

    kfree(chunk);

    file_ctx.g_koffset += len;
    file_ctx.g_uoffset = *offset;

    // msleep(30); // simulate some delay

    return total_written;
}


static ssize_t dev_read(struct file *file, char __user *buf,
                        size_t len, loff_t *offset)
{
    // Check if private_data is valid
    if (!file_ctx.file)
        return -EIO;

    size_t total_bytes_read = 0;

    char *kbuffer = kmalloc(MAX_CHUNK_SIZE, GFP_KERNEL);
    // Check if memory allocation was successful
    if (!kbuffer)
    {
        printk(KERN_ERR "Failed to allocate memory.\n");
        return -ENOMEM;
    }

    // Repeat until all data is read
    while (total_bytes_read < len)
    {
        // Calculate current chunk size
        size_t current_chunk_size = min(len - total_bytes_read, (size_t)MAX_CHUNK_SIZE);

        // Read data from the file into kernel buffer
        ssize_t bytes_read = kernel_read(file_ctx.file, kbuffer, current_chunk_size, offset);

        // Check for read errors
        if (bytes_read < 0) {
            kfree(kbuffer);
            printk(KERN_ERR "Error reading from file with code %zd\n", bytes_read);
            return total_bytes_read ? total_bytes_read : bytes_read;
        }

        // if bytes_read is 0, we've reached end of file
        if (bytes_read == 0) {
            break;
        }

        // Copy data from kernel buffer to user space
        if (copy_to_user(buf + total_bytes_read, kbuffer, bytes_read)) {
            printk(KERN_ERR "Error copying data to user space\n");
            kfree(kbuffer);
            return total_bytes_read ? total_bytes_read : -EFAULT;
        }

        // Update total bytes read
        total_bytes_read += bytes_read;

        // Short read → either EOF or partial read
        if (bytes_read < current_chunk_size)
            break;
    }

    kfree(kbuffer);

    return total_bytes_read;
}

// Define file operations structure
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = dev_open,
    .release = dev_release,
    .read = dev_read,
    .write = dev_write,
};

// Module initialization function
static int __init loop_init(void)
{
    // Register character device
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        printk(KERN_ERR "Failed to register char device\n");
        return major;
    }

    // Create device class for it
    loop_class = class_create(DEVICE_NAME);
    if (IS_ERR(loop_class)) {
        printk(KERN_ERR "Failed to create device class\n");
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(loop_class);
    }

    // Set custom devnode function to set permissions
    loop_class->devnode = set_devnode;

    // Create device node
    if (IS_ERR(device_create(loop_class, NULL, MKDEV(major, MINOR_NUM), NULL, DEVICE_NAME))) {
        printk(KERN_ERR "Failed to create device\n");
        class_destroy(loop_class);
        unregister_chrdev(major, DEVICE_NAME);
        return -ENOMEM;
    }

    printk(KERN_INFO "loop device loaded with major %d\n", major);
    return 0;
}

// Module exit function
static void __exit loop_exit(void)
{
    // Cleanup
    device_destroy(loop_class, MKDEV(major, MINOR_NUM));
    class_destroy(loop_class);
    unregister_chrdev(major, DEVICE_NAME);
    printk(KERN_INFO "loop device unloaded\n");
}

module_init(loop_init);
module_exit(loop_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eduard Hayrapetyan");
MODULE_DESCRIPTION("Kernel driver which creates char device for writing to /tmp/output file");