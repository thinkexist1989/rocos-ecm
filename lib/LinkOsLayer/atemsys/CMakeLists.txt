
cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

# Ref:
# https://gitlab.com/christophacham/cmake-kernel-module
# https://gitioc.upc.edu/open-source/xenomai-cmake/blob/master/cmake-modules/FindKernelHeaders.cmake
# https://stackoverflow.com/questions/50877135/cmake-specify-linux-kernel-module-output-build-directory

project(atemsys VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Module info
add_definitions(-D__KERNEL__ -DMODULE)

# Find the kernel release
execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE KERNEL_RELEASE
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Find the headers
find_path(
        KERNELHEADERS_DIR
        include/linux/user.h
        PATHS /usr/src/linux-headers-${KERNEL_RELEASE}
)

message(STATUS "Kernel release: ${KERNEL_RELEASE}")
message(STATUS "Kernel headers: ${KERNELHEADERS_DIR}")

# Add header files
include_directories(
        ${KERNELHEADERS_DIR}/include
)

set(DRIVER_FILE ${PROJECT_NAME}.ko)
#set(BUILD_CMD $(MAKE) -C ${KERNELHEADERS_DIR} modules M=${CMAKE_CURRENT_BINARY_DIR} src=${CMAKE_CURRENT_SOURCE_DIR})
set(BUILD_CMD make -C ${KERNELHEADERS_DIR} modules M=${CMAKE_CURRENT_BINARY_DIR} src=${CMAKE_CURRENT_SOURCE_DIR}) # build with ninja will not parse $(MAKE) to make

# Generate the Kbuild file through cmake.
#FILE(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/Kbuild "obj-m := hello.o")

add_custom_command(OUTPUT ${DRIVER_FILE}
        COMMAND ${BUILD_CMD} CONFIG_STACK_VALIDATION=
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${PROJECT_NAME}.c ${PROJECT_NAME}.bb ${PROJECT_NAME}.h VERBATIM)

add_custom_target(${PROJECT_NAME} ALL DEPENDS ${DRIVER_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_NAME}.ko ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.ko) #copy atemsys.ko to bin/

# Parse c files
#add_library(dummy_target hello.c)
















#cmake_minimum_required(VERSION 3.10)
#
#project(atemsys)
#
#set(kerneldir "" CACHE STRING "Path to the kernel build directory")
#
#if("${kerneldir}" STREQUAL "")
#    execute_process(COMMAND uname -r OUTPUT_VARIABLE uname_r
#            OUTPUT_STRIP_TRAILING_WHITESPACE)
#    set(kerneldir "/lib/modules/${uname_r}/build")
#endif()
#
#find_file(kernel_makefile NAMES Makefile
#        PATHS ${kerneldir} NO_DEFAULT_PATH)
#if(NOT kernel_makefile)
#    message(FATAL_ERROR "There is no Makefile in kerneldir!")
#endif()
#
#set(module_cmd ${CMAKE_MAKE_PROGRAM} -C ${kerneldir} M=${CMAKE_CURRENT_BINARY_DIR})
#add_custom_command(OUTPUT atemsys.ko
#        COMMAND ${module_cmd} modules
#        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
##        DEPENDS atemsys.bb atemsys.c atemsys.h
#        VERBATIM)
#add_custom_target(module DEPENDS atemsys.ko)
#add_custom_target(module-clean COMMAND ${module_cmd} clean)
#
##add_custom_target(atemsys COMMAND make all)