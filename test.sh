#!/bin/bash

# Clean up previous build artifacts
make clean > /dev/null 2>&1

# Build the kernel module
make > /dev/null 2>&1

# Define the device and output paths
DEVICE="/dev/loop"
OUTPUT_PATH="/tmp/output"

# Step 1: Unload the kernel module if it is already loaded
echo "Checking if the kernel module is loaded..."
sudo rmmod loop 2>/dev/null
if [ $? -eq 0 ]; then
    echo "Kernel module unloaded successfully."
else
    echo "Kernel module was not loaded (or error occurred)."
fi

# Step 2: Load the kernel module
echo "Loading kernel module..."
sudo insmod loop.ko
if [ $? -ne 0 ]; then
  echo "Failed to load kernel module."
  exit 1
fi

# Step 3: Generate random files of different sizes
echo "Generating random files with various sizes..."

# 1KB file
dd if=/dev/urandom of=random_1KB bs=1K count=1 > /dev/null 2>&1

# 1MB file
dd if=/dev/urandom of=random_1MB bs=1M count=1 > /dev/null 2>&1

# 100MB file
dd if=/dev/urandom of=random_100MB bs=1M count=100 > /dev/null 2>&1

# 1GB file
dd if=/dev/urandom of=random_1GB bs=1M count=1024 > /dev/null 2>&1

# 10GB file
dd if=/dev/urandom of=random_10GB bs=1M count=10240 > /dev/null 2>&1


# Step 4: Write each file to the device and compare
for file in random_1KB random_1MB random_100MB random_1GB random_10GB; do
    echo "Writing $file to $DEVICE..."
    
    # Write to the device
    cat $file > $DEVICE
    if [ $? -ne 0 ]; then
        echo "Failed to write $file to device!"
    fi
    
    # Compare the generated file with the output
    echo "Comparing $file with output file..."
    cmp -l $file $OUTPUT_PATH
    if [ $? -ne 0 ]; then
        echo "Comparison failed for $file!"
    else
        echo "Comparison successful for $file!"
    fi
done

# Step 5: Clean up generated files
echo "Cleaning up generated files..."
rm -f random_1KB random_1MB random_100MB random_1GB random_10GB

# Step 6: Unload the kernel module after testing
echo "Unloading kernel module..."
sudo rmmod loop
if [ $? -ne 0 ]; then
  echo "Failed to unload kernel module."
  exit 1
fi

echo "Kernel module tested successfully!"
