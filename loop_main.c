#include "loop.h"
#include "utils.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>

// Major number for the device
static int major;
// Device class pointer
static struct class *loop_class;
// File context
static FileContext file_ctx = {
    .file = NULL,
    .user_offset = 0,
    .local_offset = 0,
    .prev_line = {0},
    .is_prev_line_identical = false,
    .is_first_line = true,
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
    int user_access_mode = file->f_flags & O_ACCMODE;
    int open_flags = user_access_mode;

    // Set flags based on access mode
    if (user_access_mode != O_RDONLY) {
        open_flags |= O_CREAT;
        if (file->f_flags & O_TRUNC)  open_flags |= O_TRUNC;
        if (file->f_flags & O_APPEND) open_flags |= O_APPEND;
    }
    open_flags |= O_LARGEFILE;

    printk(KERN_INFO "Opening file %s with flags 0x%x\n", TMP_FILE_PATH, open_flags);

    // Open the temporary file
    struct file *tmp_file = filp_open(TMP_FILE_PATH, open_flags, 0644);
    if (IS_ERR(tmp_file)) {
        printk(KERN_ERR "Failed to open file %s\n", TMP_FILE_PATH);
        return PTR_ERR(tmp_file);
    }

    // Assign opened file to context
    file_ctx.file = tmp_file;
    return 0;
}

// Release the device file
static int dev_release(struct inode *inode, struct file *file)
{
    // Check if file descriptor is valid
    if (file_ctx.file) {
        char linebuf[128];
        // Print final hex offset line
        int flen = scnprintf(linebuf, sizeof(linebuf), "%07zx\n", (size_t)file_ctx.user_offset);
        ssize_t ret = kernel_write(file_ctx.file, linebuf, flen, &(file_ctx.local_offset));
        if (ret < 0) {
            printk(KERN_ERR "Error writing final offset line: %zd\n", ret);
        }
    }

    // Reset context 
    if (release_file_context(&file_ctx) < 0) {
        printk(KERN_ERR "Failed to release file context\n");
    }

    printk(KERN_INFO "loop device released\n");
    return 0;
}

/* Main write (keeps copy_from_user inline, no tiny copy helper, no goto) */
static ssize_t dev_write(struct file *file, const char __user *buf,
                         size_t len, loff_t *offset)
{
    // Check if private_data is valid
    if (!file_ctx.file) {
        printk(KERN_ERR "File context is invalid in write\n");
        return -EIO;
    }

    size_t total_written = 0;
    uint8_t *chunk;
    ssize_t wret;

    // Allocate memory for chunk
    chunk = kmalloc(MAX_CHUNK_SIZE, GFP_KERNEL);
    if (!chunk)
    {
        printk(KERN_ERR "Failed to allocate memory.\n");
        return -ENOMEM;
    }

    while (total_written < len) {
        // Determine current chunk size
        size_t chunkSize = min(len - total_written, MAX_CHUNK_SIZE);

        // Copy data from user space to kernel space 
        if (copy_from_user(chunk, buf + total_written, chunkSize)) {
            printk(KERN_ERR "Error copying data from user space\n");
            kfree(chunk);
            return total_written ? (ssize_t)total_written : -EFAULT;
        }

        // Process the chunk in 16-byte lines
        for (size_t ofs = 0; ofs < chunkSize; ofs += LINE_BYTES) {
            // Determine current line size
            size_t curr_chunk_size = min((size_t)LINE_BYTES, chunkSize - ofs);
            uint8_t kbuf[LINE_BYTES] = {0};
            uint16_t curr_line[WORDS_PER_LINE] = {0};

            // Parse words from the current line
            memcpy(kbuf, chunk + ofs, curr_chunk_size);
            parse_words(curr_line, kbuf, curr_chunk_size);

            // Check for repeated line
            // In case of repetition, write '*' marker
            if (!file_ctx.is_first_line &&
                memcmp(curr_line, file_ctx.prev_line, sizeof(curr_line)) == 0) {

                wret = write_repeated_line(&file_ctx);
                if (wret < 0) {
                    printk(KERN_ERR "Error writing '*' marker: %zd\n", wret);
                    kfree(chunk);
                    return total_written ? (ssize_t)total_written : wret;
                }
            } else {
                wret = write_line(&file_ctx,curr_line, curr_chunk_size);
                if (wret < 0) {
                    printk(KERN_ERR "Error writing formatted line: %zd\n", wret);
                    kfree(chunk);
                    return total_written ? (ssize_t)total_written : wret;
                }

                memcpy(file_ctx.prev_line, curr_line, sizeof(curr_line));
                file_ctx.is_prev_line_identical = false;
            }

            // Update state
            file_ctx.is_first_line = false;
            total_written += curr_chunk_size;
            file_ctx.user_offset += curr_chunk_size;
        }
    }

    kfree(chunk);

    // update user offset back to caller 
    *offset = file_ctx.user_offset;

    return (ssize_t)total_written;
}

static ssize_t dev_read(struct file *file, char __user *buf,
                        size_t len, loff_t *offset)
{
    // Check if private_data is valid
    if (!file_ctx.file)
    {
        printk(KERN_ERR "File context is invalid in read\n");
        return -EIO;
    }

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
            printk(KERN_ERR "Error reading from file with code %zd\n", bytes_read);
            kfree(kbuffer);
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
MODULE_DESCRIPTION("Kernel driver which creates char device for writing hexdump to /tmp/output file");