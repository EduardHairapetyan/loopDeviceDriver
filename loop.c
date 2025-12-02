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

// Major number for the device
static int major;
// Device class pointer
static struct class *loop_class;

// Helper function to set the devnode permissions
static char *set_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;  // Allow read and write access to all users
    return NULL;
}

// Global kernel-space offset
static loff_t g_koffset = 0;
static loff_t g_uoffset = 0;

// Open the device file
static int dev_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "loop device opened\n");

    // Reset global offset to 0 on open
    g_koffset = 0;

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

    file->private_data = tmp_file;
    return 0;
}

// Release the device file
static int dev_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "loop device released\n");

    if (file->private_data) {
        char linebuf[128];
        // Print final hex offset line
        int flen = scnprintf(linebuf, sizeof(linebuf), "%07zx\n", (size_t)g_koffset);
        kernel_write(file->private_data, linebuf, flen, &g_uoffset);
        filp_close(file->private_data, NULL);
        file->private_data = NULL;
    }

    // Reset global offset to 0 on close
    g_koffset = 0;

    printk(KERN_INFO "loop device closed\n");
    return 0;
}

// Write function using global offset
static ssize_t dev_write(struct file *file, const char __user *buf,
                         size_t len, loff_t *offset)
{
    if (!file->private_data)
        return -EIO;

    printk("global offset %lld size %ld \n", (long long)g_koffset, len);

    size_t total_written = 0;
    size_t hex_offset = g_koffset; // For display in hex dump
    char *kbuf = kmalloc(16, GFP_KERNEL);
    char linebuf[128];
    if (!kbuf)
        return -ENOMEM;

    u16 prev_line[8] = {0};
    bool prev_identical = false;
    bool first_line = true;

    while (total_written < len) {
        size_t chunk = min(len - total_written, (size_t)16);

        if (copy_from_user(kbuf, buf + total_written, chunk)) {
            kfree(kbuf);
            return total_written ? total_written : -EFAULT;
        }

        u16 curr_line[8] = {0};
        for (size_t i = 0; i < chunk; i += 2) {
            u16 w = kbuf[i];
            if (i + 1 < chunk)
                w |= kbuf[i + 1] << 8;
            curr_line[i / 2] = w;
        }

        if (!first_line && memcmp(curr_line, prev_line, chunk) == 0) {
            if (!prev_identical) {
                ssize_t written = kernel_write(file->private_data, "*\n", 2, offset);
                if (written < 0) {
                    kfree(kbuf);
                    return written;
                }
                prev_identical = true;
            }
        } else {
            int pos = scnprintf(linebuf, sizeof(linebuf), "%07zx ", hex_offset);

            for (size_t i = 0; i < chunk; i += 2) {
                u16 w = curr_line[i / 2];
                if (i == 0)
                    pos += scnprintf(linebuf + pos, sizeof(linebuf) - pos, "%04x", w);
                else
                    pos += scnprintf(linebuf + pos, sizeof(linebuf) - pos, " %04x", w);
            }

            for (size_t i = chunk; i < 16; i += 2)
                pos += scnprintf(linebuf + pos, sizeof(linebuf) - pos, "     ");

            pos += scnprintf(linebuf + pos, sizeof(linebuf) - pos, "\n");

            ssize_t written = kernel_write(file->private_data, linebuf, pos, offset);
            if (written < 0) {
                kfree(kbuf);
                return written;
            }

            memcpy(prev_line, curr_line, sizeof(curr_line));
            prev_identical = false;
        }

        first_line = false;
        total_written += chunk;
        hex_offset += chunk;
    }


    kfree(kbuf);

    // Update user-space offset by total_written
    g_koffset += len;
    g_uoffset = *offset;

    msleep(10);

    return total_written;
}


static ssize_t dev_read(struct file *file, char __user *buf,
                        size_t len, loff_t *offset)
{
    // Check if private_data is valid
    if (!file->private_data)
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
        ssize_t bytes_read = kernel_read(file->private_data, kbuffer, current_chunk_size, offset);

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