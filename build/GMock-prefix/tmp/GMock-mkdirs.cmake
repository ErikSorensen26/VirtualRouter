# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/usr/src/googletest/googlemock"
  "/home/erik/VirtualRouter/build/gmock"
  "/home/erik/VirtualRouter/build/GMock-prefix"
  "/home/erik/VirtualRouter/build/GMock-prefix/tmp"
  "/home/erik/VirtualRouter/build/GMock-prefix/src/GMock-stamp"
  "/home/erik/VirtualRouter/build/GMock-prefix/src"
  "/home/erik/VirtualRouter/build/GMock-prefix/src/GMock-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/erik/VirtualRouter/build/GMock-prefix/src/GMock-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/erik/VirtualRouter/build/GMock-prefix/src/GMock-stamp${cfgdir}") # cfgdir has leading slash
endif()
