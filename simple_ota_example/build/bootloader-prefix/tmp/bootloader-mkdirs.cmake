# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/gonzalo/esp/v5.4/esp-idf/components/bootloader/subproject"
  "/home/gonzalo/empotrados/simple_ota_example/build/bootloader"
  "/home/gonzalo/empotrados/simple_ota_example/build/bootloader-prefix"
  "/home/gonzalo/empotrados/simple_ota_example/build/bootloader-prefix/tmp"
  "/home/gonzalo/empotrados/simple_ota_example/build/bootloader-prefix/src/bootloader-stamp"
  "/home/gonzalo/empotrados/simple_ota_example/build/bootloader-prefix/src"
  "/home/gonzalo/empotrados/simple_ota_example/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/gonzalo/empotrados/simple_ota_example/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/gonzalo/empotrados/simple_ota_example/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
