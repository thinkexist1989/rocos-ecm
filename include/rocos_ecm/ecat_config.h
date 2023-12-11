//
// Created by think on 2023/12/11.
//

#ifndef ROCOS_ECM_ECAT_CONFIG_H
#define ROCOS_ECM_ECAT_CONFIG_H

#include <rocos_ecm/ecat_type.h>
#include <thread>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/format.hpp>

namespace rocos {
    class EcatConfig {
    public:
        explicit EcatConfig();

        virtual ~EcatConfig();

        void init();


        void wait();

        template<typename T>T getSlaveInputVar(int slaveId, int varId) {
            return *(T*)((char*)pdInputPtr + ecatBus->slaves[slaveId].input_vars[varId].offset);
        }


    public:

        bool getSharedMemory();
        bool getPdDataMemoryProvider();

        void waitForSignal(int id = 0); // compact code, not recommended use. use wait() instead

        std::vector<std::thread::id> threadId;

        boost::interprocess::managed_shared_memory *managedSharedMemory = nullptr;

        // PD Input and Output memory
        boost::interprocess::shared_memory_object *pdInputShm = nullptr;
        boost::interprocess::shared_memory_object *pdOutputShm = nullptr;
        boost::interprocess::mapped_region *pdInputRegion = nullptr;
        boost::interprocess::mapped_region *pdOutputRegion = nullptr;

        void *pdInputPtr = nullptr;
        void *pdOutputPtr = nullptr;

        EcatBus *ecatBus = nullptr;


        sem_t* sem_mutex[EC_SEM_NUM];

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
}


#endif //ROCOS_ECM_ECAT_CONFIG_H
