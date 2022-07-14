#[[
Copyright 2021, Yang Luo"
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

@Author
Yang Luo, PHD
Shenyang Institute of Automation, Chinese Academy of Sciences.
email: luoyang@sia.cn

@Created on: 2021.11.29
]]

cmake_minimum_required(VERSION 3.10)
project(rocos_ecm)

# add_compile_options(-std=c++11)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

## System dependencies are found with CMake's conventions
# find_package(Boost REQUIRED COMPONENTS system)
find_package(Boost REQUIRED COMPONENTS date_time filesystem system chrono)
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)

###########
## Build ##
###########

include_directories(
  include
  include/${PROJECT_NAME}
  include/sdk
)

link_directories(
  lib
)

file(GLOB_RECURSE SRCS src/*.cpp)
file(GLOB_RECURSE HDRS include/*.h include/*.hpp)
file(GLOB_RECURSE ENI_FILE config/*.xml)
file(GLOB_RECURSE CONFIG_FILE config/*.yaml)


add_executable(${PROJECT_NAME} ${SRCS})

set(DEP_LIBS
        AtemRasSrv
        EcMaster
        emllI8254x
        dl
        rt
        Threads::Threads
        Boost::boost
        Boost::date_time
        Boost::filesystem
        Boost::system
        Boost::chrono
        yaml-cpp
)

## Specify libraries to link a library or executable target against
target_link_libraries(${PROJECT_NAME}
        PRIVATE
        ${DEP_LIBS}
)

add_subdirectory(lib/LinkOsLayer/atemsys)

file(COPY lib/libemllI8254x.so DESTINATION ${CMAKE_BINARY_DIR}/bin)
file(COPY ${ENI_FILE} ${CONFIG_FILE} DESTINATION ${CMAKE_BINARY_DIR}/bin)

configure_file(scripts/initECM.sh.in  ${CMAKE_BINARY_DIR}/initECM.sh @ONLY)
file(COPY ${CMAKE_BINARY_DIR}/initECM.sh
     DESTINATION ${CMAKE_BINARY_DIR}/bin
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

configure_file(scripts/runECM.sh.in   ${CMAKE_BINARY_DIR}/runECM.sh @ONLY)
file(COPY ${CMAKE_BINARY_DIR}/runECM.sh
     DESTINATION ${CMAKE_BINARY_DIR}/bin
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

configure_file(scripts/rocos-ecm.service.in   ${CMAKE_BINARY_DIR}/rocos-ecm.service @ONLY)
file(COPY ${CMAKE_BINARY_DIR}/rocos-ecm.service
        DESTINATION ${CMAKE_BINARY_DIR}/bin
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )

#############
## Install ##
#############
install(TARGETS ${PROJECT_NAME}
        EXPORT RocosEcmTargets
        LIBRARY DESTINATION lib  # 动态库安装路径
        ARCHIVE DESTINATION lib  # 静态库安装路径
        RUNTIME DESTINATION bin  # 可执行文件安装路径
        PUBLIC_HEADER DESTINATION include  # 头文件安装路径
        )

# 写入库的版本信息
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        RocosEcmConfigVersion.cmake
        VERSION ${PACKAGE_VERSION}
        COMPATIBILITY AnyNewerVersion  # 表示该函数库向下兼容
)

install(EXPORT RocosEcmTargets
        FILE RocosEcmTargets.cmake
#        NAMESPACE rocos::ecm::
        DESTINATION lib/cmake/rocos-ecm
        )


#############
## Testing ##
#############

enable_testing()
add_executable(unit_test test/unit_test.cpp)
target_link_libraries(unit_test
        PRIVATE
        ${DEP_LIBS}
        )
add_test(NAME unit_test COMMAND unit_test)
