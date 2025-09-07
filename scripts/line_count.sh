
#!/bin/bash

# Directory to search
SRC_DIR="../VirtualRouter/src"

# Find all cpp, h, and hpp files, then count lines
find "$SRC_DIR" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) \
  -exec wc -l {} + | awk '
  BEGIN { total=0 }
  {
    if ($2 != "total") {
      print $0
      total += $1
    }
  }
  END { print "----------------------"; print "Total lines:", total }
  '
