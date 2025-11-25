#include <linux/device.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DEVICE_NAME     "loop"
#define TMP_FILE_PATH   "/tmp/output"
#define MINOR_NUM       0
// Maximum current_chunk_size size for read/write operations
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

// Open the device file
static int dev_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "loop device opened\n");
    
    // Determine user access mode
    int user_access_mode = file->f_flags & O_ACCMODE;
    int open_flags = user_access_mode;

    // If access mode is write or read-write, set create/truncate/append flags based on f_flags
    if (user_access_mode != O_RDONLY) {
        open_flags |= O_CREAT;
        if (file->f_flags & O_TRUNC)  open_flags |= O_TRUNC;
        if (file->f_flags & O_APPEND) open_flags |= O_APPEND;
    }
    // Enable large file support so that files larger than 2GB can be handled
    open_flags |= O_LARGEFILE;

    printk(KERN_INFO "Opening file %s with flags 0x%x\n", TMP_FILE_PATH, open_flags);
    
    // Open the temporary file
    struct file *tmp_file = filp_open(TMP_FILE_PATH, open_flags, 0644);
    if (IS_ERR(tmp_file)) {
        printk(KERN_ERR "Failed to open file %s\n", TMP_FILE_PATH);
        return PTR_ERR(tmp_file);
    }

    // Store pointer per open instance
    file->private_data = tmp_file;

    return 0;
}

// Release the device file
static int dev_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "loop device released\n");

    // Close the temporary file if it was opened
    if (file->private_data) {
        filp_close(file->private_data, NULL);
        file->private_data = NULL;
    }
    
    printk(KERN_INFO "loop device closed\n");
    return 0;
}

// Write data to the temporary file
static ssize_t dev_write(struct file *file, const char __user *buf,
                         size_t len, loff_t *offset)
{
    // Check if private_data is valid
    if (!file->private_data)
        return -EIO;

    size_t total_bytes_written = 0;

    char *kbuffer = kmalloc(MAX_CHUNK_SIZE, GFP_KERNEL);

    // Check if memory allocation was successful
    if (!kbuffer)
        return -ENOMEM;

    // Repeat until all data is written
    while (total_bytes_written < len)
    {
        // Calculate current chunk size
        size_t current_chunk_size = min(len - total_bytes_written, (size_t)MAX_CHUNK_SIZE);

        // Copy data from user space to kernel buffer
        if (copy_from_user(kbuffer, buf + total_bytes_written, current_chunk_size)) {
            printk(KERN_ERR "Error copying data from user space\n");
            kfree(kbuffer);
            return total_bytes_written ? total_bytes_written : -EFAULT;
        }

        // Write data from kernel buffer to the file
        ssize_t bytes_written = kernel_write(file->private_data, kbuffer, current_chunk_size, offset);

        // Check for write errors
        if (bytes_written < 0)
        {
            kfree(kbuffer);
            printk(KERN_ERR "Error writing to file with code %zd\n", bytes_written);
            return total_bytes_written ? total_bytes_written : bytes_written;
        }            

        // Update total bytes written
        total_bytes_written += bytes_written;

        // Check if current_chunk_size was bytes_written completely
        if (bytes_written < current_chunk_size)
            break;
    }

    kfree(kbuffer);

    return total_bytes_written;
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