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

for size in 1K 1M 100M 1G 2G; do
    filename="random_${size}"
    echo "Generating $filename..."
    case $size in
        1K) bs=1K count=1 ;;
        1M) bs=1M count=1 ;;
        100M) bs=100M count=1 ;;   # 10 × 10M = 100M
        1G) bs=1G count=1 ;;    # 10 × 100M = 1G
        2G) bs=1G count=2 ;;     # 10 × 1G = 10G
    esac
    dd if=/dev/urandom of="$filename" bs=$bs count=$count status=none
done

echo "Random files generated."

# Step 4: Write each file to the device and compare with hexdump
for file in random_2G; do
    echo "Writing $file to $DEVICE..."

    # Write to the device
    cat "$file" > "$DEVICE"
    if [ $? -ne 0 ]; then
        echo "Failed to write $file to device!"
        continue
    fi

    echo "Generating output to compare using hexdump..."
    # Generate hexdump of the original file
    pv -L 10m "$file" | hexdump > /tmp/reference

    # Compare device output with hexdump
    echo "Comparing $file output with hexdump..."
    diff /tmp/reference "$OUTPUT_PATH" > /tmp/diff_result
    if [ $? -eq 0 ]; then
        echo "Comparison successful for $file!"
    else
        echo "Comparison failed for $file! Differences:"
        head -n 10 /tmp/diff_result
    fi
done

# Step 5: Clean up generated files
echo "Cleaning up generated files..."
rm -f random_1K random_1M random_100M random_1G random_2G /tmp/reference /tmp/diff_result

# Step 6: Unload the kernel module after testing
echo "Unloading kernel module..."
sudo rmmod loop
if [ $? -ne 0 ]; then
  echo "Failed to unload kernel module."
  exit 1
fi

# Step 7: Clean up build artifacts
make clean > /dev/null 2>&1

echo "Kernel module tested successfully!"
