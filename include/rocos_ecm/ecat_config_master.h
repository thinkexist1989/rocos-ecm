/*
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
@email: yluo@hit.edu.cn

@Created on: 2021.11.29
@Last Modified: 2023.3.28 22:03
*/


/*-----------------------------------------------------------------------------
 * ecat_config.hpp
 * Description              EtherCAT Configurations
 *
 *---------------------------------------------------------------------------*/

#ifndef ECAT_CONFIG_MASTER_H_INCLUDED
#define ECAT_CONFIG_MASTER_H_INCLUDED

#include <iostream>
#include <sstream>

#include <boost/format.hpp>
#include <boost/timer/timer.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>


#include <string>
#include <cstring>
#include <cstdlib>
#include <unordered_map>

#include <sys/mman.h> //shm_open() mmap()
#include <unistd.h>   // ftruncate()
#include <fcntl.h>
#include <sys/stat.h>  //umask

#include <cmath>
#include <thread>


#include <rocos_ecm/ecat_type.h>

/** Class RobotConfig contains all configurations of the robot
 * 
 */
class EcatConfigMaster {
public:

    explicit EcatConfigMaster(int id = 0);
    virtual ~EcatConfigMaster();

public:

    bool createSharedMemory();

    bool getSharedMemory();

    bool createPdDataMemoryProvider(int pdInputSize, int pdOutputSize);

    bool getPdDataMemoryProvider();

    void init();

    void waitForSignal(int id = 0); // compact code, not recommended use. use wait() instead

    void wait();



    ///////////// Format robot info /////////////////
    std::string to_string();


public:
    rocos::EcatBus *ecatBus = nullptr;

    boost::interprocess::managed_shared_memory *managedSharedMemory = nullptr;

    // PD Input and Output memory
    boost::interprocess::shared_memory_object *pdInputShm = nullptr;
    boost::interprocess::shared_memory_object *pdOutputShm = nullptr;
    boost::interprocess::mapped_region *pdInputRegion = nullptr;
    boost::interprocess::mapped_region *pdOutputRegion = nullptr;
    void* pdInputPtr = nullptr;
    void* pdOutputPtr = nullptr;


    sem_t* sem_mutex[EC_SEM_NUM];

protected:

    std::vector<std::thread::id> threadId;
    std::string ecmName {EC_SHM};
    std::string mutexName {EC_SEM_MUTEX};
    std::string pdInputName {"pd_input"};
    std::string pdOutputName {"pd_output"};




    //////////// OUTPUT FORMAT SETTINGS ////////////////////
    //Terminal Color Show
    enum Color {
        BLACK = 0,
        RED = 1,
        GREEN = 2,
        YELLOW = 3,
        BLUE = 4,
        MAGENTA = 5,
        CYAN = 6,
        WHITE = 7,
        DEFAULT = 9
    };

    enum MessageLevel {
        NORMAL = 0,
        WARNING = 1,
        ERROR = 2
    };


    boost::format _f{"\033[1;3%1%m "};       //设置前景色
    boost::format _b{"\033[1;4%1%m "};       //设置背景色
    boost::format _fb{"\033[1;3%1%;4%2%m "}; //前景背景都设置
    boost::format _def{"\033[0m "};          //恢复默认

    void print_message(const std::string &msg, MessageLevel msgLvl = MessageLevel::NORMAL);
};

#endif //ifndef ECAT_CONFIG_MASTER_H_INCLUDED
