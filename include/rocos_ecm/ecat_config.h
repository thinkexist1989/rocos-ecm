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

#ifndef ECAT_CONFIG_HPP_INCLUDED
#define ECAT_CONFIG_HPP_INCLUDED

#include <yaml-cpp/yaml.h>
#include <iostream>
#include <sstream>

#include <boost/format.hpp>
#include <boost/timer/timer.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/chrono.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/containers/string.hpp>

#include <string>
#include <cstring>
#include <cstdlib>
#include <unordered_map>

#include <sys/mman.h> //shm_open() mmap()
#include <unistd.h>   // ftruncate()
#include <fcntl.h>
#include <semaphore.h> //sem
#include <sys/stat.h>  //umask

#include <cmath>
#include <thread>


enum INPUTS //
{
    INPUT_GRP_NAME, // group_name
    STATUS_WORD,
    POSITION_ACTUAL_VALUE,
    VELOCITY_ACTUAL_VALUE,
    TORQUE_ACTUAL_VALUE,
    LOAD_TORQUE_VALUE,
    SECONDARY_POSITION_VALUE, // Synapticon Driver Compat 0x230A
    SECONDARY_VELOCITY_VALUE, // Synapticon Driver Compat 0x230B
};
enum OUTPUTS {
    OUTPUT_GRP_NAME, // group_name
    MODE_OF_OPERATION,
    CONTROL_WORD,
    TARGET_POSITION,
    TARGET_VELOCITY,
    TARGET_TORQUE
};

typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> CharAlloc;
typedef boost::interprocess::basic_string<char, std::char_traits<char>, CharAlloc> EcString;
typedef boost::interprocess::allocator<EcString, boost::interprocess::managed_shared_memory::segment_manager> StringAlloc;
typedef boost::interprocess::vector<EcString, StringAlloc> EcStringVec;

struct EcatSlaveInfo;
typedef boost::interprocess::allocator<EcatSlaveInfo, boost::interprocess::managed_shared_memory::segment_manager> EcSlaveAlloc;
typedef boost::interprocess::vector<EcatSlaveInfo, EcSlaveAlloc> EcSlaveVec;

struct EcatSlaveInfo {

    uint32_t slave_id{0};

    /** Struct store the EtherCAT process data input  **/
    struct PDInput {
        uint16_t status_word{0};             // Size: 2.0 unsigned
        int32_t position_actual_value{0};    // Size: 4.0 signed
        int32_t velocity_actual_value{0};    // Size: 4.0 signed
        int16_t torque_actual_value{0};      // Size: 2.0 signed
        int16_t load_torque_value{0};        // Size: 2.0 signed
        int32_t secondary_position_value{0}; // Size: 4.0 signed -> Synapticon Driver Compat
        int32_t secondary_velocity_value{0}; // Size: 4.0 signed -> Synapticon Driver Compat
    };

    PDInput inputs;

    /** Struct store the EtherCAT process data output  **/
    struct PDOutput {
        int8_t mode_of_operation{8}; // Size: 1.0 signed
        uint16_t control_word{0};    // Size: 2.0 unsigned
        int32_t target_position{0};  // Size: 4.0 signed
        int32_t target_velocity{0};  // Size: 4.0 signed
        int16_t target_torque{0};    // Size: 2.0 signed
    };

    PDOutput outputs;
};

/////////////////////   STRUCT DEFINITION   /////////////////////////////////
struct EcatInfo {

    boost::chrono::time_point<boost::chrono::system_clock> timestamp;  // Timestamp

    enum EcatState {
        UNKNOWN = 0,
        INIT = 1,
        PREOP = 2,
        SAFEOP = 4,
        OP = 8,

        BOOTSTRAP = 3
    };

    double minCyclcTime{0.0}; // minimum cycling time   /* usec */
    double maxCycleTime{0.0}; // maximum cycling time  /* usec */
    double avgCycleTime{0.0}; // average cycling time  /* usec */
    double currCycleTime{0.0}; // current cycling time /* usec */

    EcatState ecatState{UNKNOWN};    // State of Ec-Master

    EcatState ecatRequestState {OP};  // Request State of Ec-Master

    EcatState ecatNextExpectedState {INIT};  // Next Expected State of Ec-Master

    int32_t slave_number{0};

    bool isAuthorized{false}; // if the master is authorized

//    EcVec slaves; // all the slaves data
//    std::vector<EcatSlaveInfo> slaves; // all the slaves data
//     EcatSlaveInfo slaves[MAX_SLAVE_NUM];

};


/** Class SlaveConfig contains all configurations of one joint
 * 
 */
class SlaveConfig {
public:
    SlaveConfig() {
        // EtherCAT Process Data Input default Name Mapping
        ecInpMap[INPUT_GRP_NAME] = "Inputs";
        ecInpMap[STATUS_WORD] = "Status word";
        ecInpMap[POSITION_ACTUAL_VALUE] = "Position actual value";
        ecInpMap[VELOCITY_ACTUAL_VALUE] = "Velocity actual value";
        ecInpMap[TORQUE_ACTUAL_VALUE] = "Torque actual value";
        ecInpMap[LOAD_TORQUE_VALUE] = "Analog Input 1";
        ecInpMap[SECONDARY_POSITION_VALUE] = "Secondary position value"; // Synapticon Driver Compat
        ecInpMap[SECONDARY_VELOCITY_VALUE] = "Secondary velocity value"; // Synapticon Driver Compat

        ecInpOffsets[STATUS_WORD] = 0;
        ecInpOffsets[POSITION_ACTUAL_VALUE] = 0;
        ecInpOffsets[VELOCITY_ACTUAL_VALUE] = 0;
        ecInpOffsets[TORQUE_ACTUAL_VALUE] = 0;
        ecInpOffsets[LOAD_TORQUE_VALUE] = 0;

        // EtherCAT Process Data Output default Name Mapping
        ecOutpMap[OUTPUT_GRP_NAME] = "Outputs";
        ecOutpMap[MODE_OF_OPERATION] = "Mode of operation";
        ecOutpMap[CONTROL_WORD] = "Control word";
        ecOutpMap[TARGET_POSITION] = "Target Position";
        ecOutpMap[TARGET_VELOCITY] = "Target Velocity";
        ecOutpMap[TARGET_TORQUE] = "Target Torque";

        ecOutpOffsets[MODE_OF_OPERATION] = 0;
        ecOutpOffsets[CONTROL_WORD] = 0;
        ecOutpOffsets[TARGET_POSITION] = 0;
        ecOutpOffsets[TARGET_VELOCITY] = 0;
        ecOutpOffsets[TARGET_TORQUE] = 0;

    }

    ~SlaveConfig() {

    }

    int id{0};

//    std::string jntName;

    std::string name{"Slave_1001 [Elmo Drive ]"};
    std::map<INPUTS, std::string> ecInpMap;
    std::map<OUTPUTS, std::string> ecOutpMap;

    std::map<INPUTS, int> ecInpOffsets;
    std::map<OUTPUTS, int> ecOutpOffsets;

//    T_JOINT_EC_INPUT *jntEcInpPtr = nullptr;
//    T_JOINT_EC_OUTPUT *jntEcOutpPtr = nullptr;
};


/** Class RobotConfig contains all configurations of the robot
 * 
 */
class EcatConfig {
public:

    explicit EcatConfig(std::string configFile = "ecat_config.yaml");

    virtual ~EcatConfig();

    std::string configFileName{};

    std::string name{"default_robot"};

    std::string license{"12345678-12345678-12345678F"};

    uint32_t loop_hz{1000}; // Reserved

    int slave_number{0};

    std::vector<SlaveConfig> slaveCfg;


    std::vector<sem_t *> sem_mutex; // DO NOT USE

    EcatInfo *ecatInfo = nullptr;
    EcSlaveVec *ecatSlaveVec = nullptr;
    EcStringVec *ecatSlaveNameVec = nullptr;
    boost::interprocess::managed_shared_memory *managedSharedMemory = nullptr;

    // PD Input and Output memory
    boost::interprocess::shared_memory_object *pdInputShm = nullptr;
    boost::interprocess::shared_memory_object *pdOutputShm = nullptr;
    boost::interprocess::mapped_region *pdInputRegion = nullptr;
    boost::interprocess::mapped_region *pdOutputRegion = nullptr;
    void* pdInputPtr = nullptr;
    void* pdOutputPtr = nullptr;


public:


    bool parserYamlFile(const std::string &configFile);

    bool parserYamlFile();


    std::string getEcInpVarName(int jntId, INPUTS enumEcInp);

    std::string getEcOutpVarName(int jntId, OUTPUTS enumEcOutp);

    bool createSharedMemory();

    bool createPdDataMemoryProvider(int pdInputSize, int pdOutputSize);

    bool getPdDataMemoryProvider();



    /// \brief Get shared memory of rocos_ecm
    /// \return true if ok
    void init();

    bool getSharedMemory();

    ////////////// Get joints info for Ec Input /////////////////////

    inline int32_t getActualPositionEC(int id) const { return ecatSlaveVec->at(id).inputs.position_actual_value; }

    inline int32_t getActualVelocityEC(int id) const { return ecatSlaveVec->at(id).inputs.velocity_actual_value; }

    inline int16_t getActualTorqueEC(int id) const { return ecatSlaveVec->at(id).inputs.torque_actual_value; }

    inline int16_t getLoadTorqueEC(int id) const { return ecatSlaveVec->at(id).inputs.load_torque_value; }

    inline uint16_t getStatusWordEC(int id) const { return ecatSlaveVec->at(id).inputs.status_word; }

    inline int32_t getSecondaryPositionEC(int id) const {
        return ecatSlaveVec->at(id).inputs.secondary_position_value;
    }

    inline int32_t getSecondaryVelocityEC(int id) const {
        return ecatSlaveVec->at(id).inputs.secondary_velocity_value;
    }

    ////////////// Get joints info for Ec Input /////////////////////

    inline void setTargetPositionEC(int id, int32_t pos) { ecatSlaveVec->at(id).outputs.target_position = pos; }

    inline void setTargetVelocityEC(int id, int32_t vel) { ecatSlaveVec->at(id).outputs.target_velocity = vel; }

    inline void setTargetTorqueEC(int id, int16_t tor) { ecatSlaveVec->at(id).outputs.target_torque = tor; }

    inline void setModeOfOperationEC(int id, int8_t mode) { ecatSlaveVec->at(id).outputs.mode_of_operation = mode; }

    inline void
    setControlwordEC(int id, uint16_t ctrlword) { ecatSlaveVec->at(id).outputs.control_word = ctrlword; }

    void waitForSignal(int id = 0); // compact code, not recommended use. use wait() instead

    void wait();


    ///////////// Format robot info /////////////////
    std::string to_string();

protected:

    std::vector<std::thread::id> threadId;



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

#endif //ifndef ECAT_CONFIG_HPP_INCLUDED
