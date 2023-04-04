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
project(rocos_ecm VERSION 0.0.3 DESCRIPTION "ROCOS EtherCAT Master")

# add_compile_options(-std=c++11)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# rocos-ecm install path
include(GNUInstallDirs)
set(CMAKE_INSTALL_PREFIX /opt/rocos/ecm)
message(STATUS "INSTALL PREFIX: ${CMAKE_INSTALL_PREFIX}")
#message(STATUS "INSTALL INCLUDE DIR: ${CMAKE_INSTALL_INCLUDEDIR}")
#message(STATUS "INSTALL LIB DIR: ${CMAKE_INSTALL_LIBDIR}")
#message(STATUS "INSTALL BIN DIR: ${CMAKE_INSTALL_BINDIR}")

## System dependencies are found with CMake's conventions
# find_package(Boost REQUIRED COMPONENTS system)
find_package(Boost REQUIRED COMPONENTS date_time filesystem system chrono)
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)


#find_package(gflags REQUIRED)
#find_package(glog REQUIRED)
add_subdirectory(3rdparty/gflags)
#add_subdirectory(3rdparty/glog)
add_subdirectory(3rdparty/termcolor)

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

file(GLOB_RECURSE SRCS src/*.cpp EXCLUDE src/ecat_config.cpp)
file(GLOB_RECURSE HDRS include/*.h include/*.hpp)
file(GLOB_RECURSE ENI_FILE config/*.xml)
file(GLOB_RECURSE CONFIG_FILE config/*.yaml)

# ecat_config library
add_library(ecat_config SHARED src/ecat_config.cpp)
add_library(${PROJECT_NAME}::ecat_config ALIAS ecat_config)
target_include_directories(ecat_config
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        )

target_link_libraries(ecat_config
        PUBLIC
        Boost::boost
        Boost::date_time
        Boost::filesystem
        Boost::system
        Boost::chrono
        yaml-cpp
        rt
        )

# rocos_ecm executable
add_executable(${PROJECT_NAME} ${SRCS})
target_link_libraries(${PROJECT_NAME}
        PRIVATE
        AtemRasSrv
        EcMaster
        emllI8254x
        dl
        rt
        Threads::Threads
        gflags::gflags
        ecat_config
        pthread
        termcolor::termcolor
        )



add_subdirectory(lib/LinkOsLayer/atemsys)

file(COPY lib/libemllI8254x.so DESTINATION ${CMAKE_BINARY_DIR}/bin)
file(COPY ${ENI_FILE} ${CONFIG_FILE} DESTINATION ${CMAKE_BINARY_DIR}/bin)

configure_file(scripts/initECM.sh.in ${CMAKE_BINARY_DIR}/initECM.sh @ONLY)
file(COPY ${CMAKE_BINARY_DIR}/initECM.sh
        DESTINATION ${CMAKE_BINARY_DIR}/bin
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )

configure_file(scripts/runECM.sh.in ${CMAKE_BINARY_DIR}/runECM.sh @ONLY)
file(COPY ${CMAKE_BINARY_DIR}/runECM.sh
        DESTINATION ${CMAKE_BINARY_DIR}/bin
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )

configure_file(scripts/rocos-ecm.service.in ${CMAKE_BINARY_DIR}/rocos-ecm.service @ONLY)
file(COPY ${CMAKE_BINARY_DIR}/rocos-ecm.service
        DESTINATION ${CMAKE_BINARY_DIR}/bin
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )

configure_file(include/ver.h.in ver.h) # Version Definition

#############
## Install ##
#############

# Add support for installation
include(CMakePackageConfigHelpers)

# Install headers
#install(DIRECTORY include/rocos_ecm
#        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
#        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
#        )

#install(DIRECTORY include/sdk
#        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
#        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
#        )

install(FILES ${CMAKE_BINARY_DIR}/ver.h include/rocos_ecm/ecat_config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rocos_ecm
        )

# Install binaries
install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # 动态库安装路径
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}  # 静态库安装路径
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # 可执行文件安装路径
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}  # 头文件安装路径
        )

install(TARGETS ecat_config
        EXPORT ecat_config-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # 动态库安装路径
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}  # 静态库安装路径
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # 可执行文件安装路径
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}  # 头文件安装路径
        )

# Install config files
file(GLOB SO_FILES  ${CMAKE_BINARY_DIR}/bin/*.so)
file(GLOB KO_FILES  ${CMAKE_BINARY_DIR}/bin/*.ko)
file(GLOB SH_FILES  ${CMAKE_BINARY_DIR}/bin/*.sh)
install(FILES ${SO_FILES} ${KO_FILES} ${SH_FILES}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

set(${PROJECT_NAME}_INSTALL_CONFIGDIR ${CMAKE_INSTALL_PREFIX}/config)
install(FILES  ${ENI_FILE} ${CONFIG_FILE}
        DESTINATION ${${PROJECT_NAME}_INSTALL_CONFIGDIR}
        )


# install CMake config files
set(${PROJECT_NAME}_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
install(EXPORT ${PROJECT_NAME}-targets
        FILE ${PROJECT_NAME}-targets.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${${PROJECT_NAME}_INSTALL_CMAKEDIR}
        )

install(EXPORT ecat_config-targets
        FILE ecat_config-targets.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${${PROJECT_NAME}_INSTALL_CMAKEDIR}
        )



# 生成并安装配置文件
configure_package_config_file(
        cmake/${PROJECT_NAME}-config.cmake.in
        ${PROJECT_NAME}-config.cmake
        INSTALL_DESTINATION ${${PROJECT_NAME}_INSTALL_CMAKEDIR}
)

# 写入库的版本信息
write_basic_package_version_file(
        ${PROJECT_NAME}-config-version.cmake
        VERSION ${PACKAGE_VERSION}
        COMPATIBILITY AnyNewerVersion  # 表示该函数库向下兼容
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
        DESTINATION ${${PROJECT_NAME}_INSTALL_CMAKEDIR}
        )

#############
## Testing ##
#############

enable_testing()
add_executable(unit_test test/unit_test.cpp)
target_link_libraries(unit_test
        PRIVATE
        ecat_config
        )
add_test(NAME unit_test COMMAND unit_test)
