#!/bin/bash

# Specify the exact directory to recurse into
SPECIFIC_DIR=".."  # Replace with your desired directory

# Specify the exact location for the output file
OUTPUT_FILE="combined_code.txt"  # Replace with your desired output file path

# Specify the special directory and the files to process within it
SPECIAL_DIR="$SPECIFIC_DIR/VirtualRouter/Common"
SPECIAL_FILES=(
    "$SPECIAL_DIR/Authentication.hpp"
    "$SPECIAL_DIR/ByteString.hpp"
    "$SPECIAL_DIR/Functions.h"
    "$SPECIAL_DIR/Functions.cpp"
    "$SPECIAL_DIR/Profiler.h"
)

# Ensure the specified directory exists
if [ ! -d "$SPECIFIC_DIR" ]; then
    echo "Error: Directory '$SPECIFIC_DIR' does not exist."
    exit 1
fi

# Remove the output file if it already exists
if [ -f "$OUTPUT_FILE" ]; then
    echo "Removing existing output file: $OUTPUT_FILE"
    rm "$OUTPUT_FILE"
fi

echo "Recursing into directory: $SPECIFIC_DIR"
echo "Special filters applied to: $SPECIAL_DIR"
echo "Concatenating results into: $OUTPUT_FILE"

# Process files in the special directory
if [ -d "$SPECIAL_DIR" ]; then
    echo "Processing files in the special directory: $SPECIAL_DIR"
    for file in "${SPECIAL_FILES[@]}"; do
        if [ -f "$file" ]; then
            echo "Processing special file: $file"
            echo -e "\n\n// ======= Start of $file =======\n" >> "$OUTPUT_FILE"
            cat "$file" >> "$OUTPUT_FILE"
            echo -e "\n// ======= End of $file =======\n" >> "$OUTPUT_FILE"
        else
            echo "Warning: Special file '$file' does not exist. Skipping."
        fi
    done
else
    echo "Warning: Special directory '$SPECIAL_DIR' does not exist. Skipping."
fi

# Recursively find and process all .h, .hpp, and .cpp files, excluding specific directories
find "$SPECIFIC_DIR" -type f \( -iname "*.h" -o -iname "*.hpp" -o -iname "*.cpp" \) \
    ! -path "$SPECIFIC_DIR/build/*" \
    ! -path "$SPECIFIC_DIR/VirtualRouter/Common/*" \
    ! -path "$SPECIFIC_DIR/Utils/*" | while read -r file; do
    echo "Processing file: $file"
    echo -e "\n\n// ======= Start of $file =======\n" >> "$OUTPUT_FILE"
    cat "$file" >> "$OUTPUT_FILE"
    echo -e "\n// ======= End of $file =======\n" >> "$OUTPUT_FILE"
done

echo "All files have been concatenated into: $OUTPUT_FILE"

# ================================
# Extraction of Class Declarations
# ================================

# Define extraction parameters
EXTRACTION_OUTPUT_FILE="classes_list.txt"

# Remove the extraction output file if it already exists
if [ -f "$EXTRACTION_OUTPUT_FILE" ]; then
    echo "Removing existing extraction output file: $EXTRACTION_OUTPUT_FILE"
    rm "$EXTRACTION_OUTPUT_FILE"
fi

echo "Extracting class declarations from '$OUTPUT_FILE' to '$EXTRACTION_OUTPUT_FILE'..."

# Use SED to extract class declarations
sed -n '/^\s*class\s\+\w\+.*$/{
    N
    /^\s*class\s\+\w\+.*\n\s*{/p
}' combined_code.txt > classes_list
