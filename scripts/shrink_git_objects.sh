#!/bin/bash

# Check if BFG is installed
if ! command -v java &> /dev/null || [ ! -f bfg.jar ]; then
  echo "BFG Repo-Cleaner (bfg.jar) not found. Download it from https://rtyley.github.io/bfg-repo-cleaner/ and place it in the same directory as this script."
  exit 1
fi

# Function to remove large files from history with BFG
remove_large_files() {
  # Edit this pattern to match the large file types you want to remove
  LARGE_FILE_PATTERN="*.zip"  # Update as needed
  echo "Removing large files matching pattern: $LARGE_FILE_PATTERN"
  java -jar bfg.jar --delete-files "$LARGE_FILE_PATTERN" .
}

# Step 1: Remove large files from history
remove_large_files

# Step 2: Expire reflog entries and prune unreachable objects
echo "Expiring reflog entries and pruning unreachable objects..."
git reflog expire --expire=now --all
git gc --prune=now

# Step 3: Run aggressive garbage collection
echo "Running aggressive garbage collection..."
git gc --aggressive --prune=now

echo "Repository cleanup complete!"
