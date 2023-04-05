//
// Created by Yang Luo on 3/27/23.
//

#include "ecat_config.h"

/*-SHARED MEMORY DEFINITION-------------------------------------*/
#define EC_SHM "ecm"
#define EC_SHM_MAX_SIZE 65536

#define EC_SEM_MUTEX "sync"
#define EC_SEM_NUM 10

EcatConfig::EcatConfig(std::string configFile) : configFileName(configFile), sem_mutex(EC_SEM_NUM) {

}

EcatConfig::~EcatConfig() {

}

bool EcatConfig::parserYamlFile(const std::string &configFile) {
    if (!boost::filesystem::exists(configFile)) {
        print_message("[YAML] Can not find the config file.", MessageLevel::ERROR);
        return false;
    }

    YAML::Node config = YAML::LoadFile(configFile);

    if (!config["robot"]) {
        print_message("[YAML] Can not find the robot label.", MessageLevel::ERROR);
        return false;
    }

    YAML::Node robot = config["robot"];

    name = robot["name"].as<std::string>();
    print_message("[YAML] Robot name is: " + name, MessageLevel::NORMAL);

    if (robot["license"])
        license = robot["license"].as<std::string>();

    loop_hz = robot["loop_hz"].as<uint32_t>();

    slave_number = robot["slave_number"].as<int>();
    if (slave_number == robot["slaves"].size()) {
        print_message((boost::format("[YAML] Robot has %d slaves") % slave_number).str(), MessageLevel::NORMAL);
    } else {
        print_message("[YAML] Robot has bad number of slaves.", MessageLevel::ERROR);
        return false;
    }

    YAML::Node slaves = robot["slaves"];
    slaveCfg.resize(slave_number);

    std::set<int> isJntOK;
    for (int i = 0; i < slave_number; i++) {

        /// slaves.id
        int id = slaves[i]["id"].as<int>();
        if (id >= 0) {
            print_message((boost::format("[YAML] -- Joint ID: %d. ") % id).str(), MessageLevel::NORMAL);
        } else {
            print_message("[YAML] Bad slaves ID!! ", MessageLevel::ERROR);
            return false;
        }
        auto res = isJntOK.insert(id);
        if (!res.second) { //Found duplicate elements
            print_message("[YAML] Bad slaves ID is DUPLICATE!! ", MessageLevel::ERROR);
            return false;
        }

//            //name
//            slaveCfg[id].jntName = slaves[i]["name"].as<std::string>();
//            print_message((boost::format("[YAML] -- Joint name: %s. ") % slaveCfg[id].jntName).str(),
//                          MessageLevel::NORMAL);

        /// slaves[].name
        slaveCfg[id].name = slaves[i]["name"].as<std::string>();
        print_message((boost::format("[YAML] -- Slave name: %s .") % slaveCfg[id].name).str(),
                      MessageLevel::NORMAL);

        /// Process Data Input Mapping
        if (slaves[i]["inputs"]["group_name"])
            slaveCfg[id].ecInpMap[INPUT_GRP_NAME] = slaves[i]["inputs"]["group_name"].as<std::string>();
        if (slaves[i]["inputs"]["status_word"])
            slaveCfg[id].ecInpMap[STATUS_WORD] = slaves[i]["inputs"]["status_word"].as<std::string>();
        if (slaves[i]["inputs"]["position_actual_value"])
            slaveCfg[id].ecInpMap[POSITION_ACTUAL_VALUE] = slaves[i]["inputs"]["position_actual_value"].as<std::string>();
        if (slaves[i]["inputs"]["velocity_actual_value"])
            slaveCfg[id].ecInpMap[VELOCITY_ACTUAL_VALUE] = slaves[i]["inputs"]["velocity_actual_value"].as<std::string>();
        if (slaves[i]["inputs"]["torque_actual_value"])
            slaveCfg[id].ecInpMap[TORQUE_ACTUAL_VALUE] = slaves[i]["inputs"]["torque_actual_value"].as<std::string>();
        if (slaves[i]["inputs"]["load_torque_value"])
            slaveCfg[id].ecInpMap[LOAD_TORQUE_VALUE] = slaves[i]["inputs"]["load_torque_value"].as<std::string>();
        if (slaves[i]["inputs"]["secondary_position_value"])
            slaveCfg[id].ecInpMap[SECONDARY_POSITION_VALUE] = slaves[i]["inputs"]["secondary_position_value"].as<std::string>();
        if (slaves[i]["inputs"]["secondary_velocity_value"])
            slaveCfg[id].ecInpMap[SECONDARY_VELOCITY_VALUE] = slaves[i]["inputs"]["secondary_velocity_value"].as<std::string>();

        /// Process Data Out Mapping
        if (slaves[i]["outputs"]["group_name"])
            slaveCfg[id].ecOutpMap[OUTPUT_GRP_NAME] = slaves[i]["outputs"]["group_name"].as<std::string>();
        if (slaves[i]["outputs"]["mode_of_operation"])
            slaveCfg[id].ecOutpMap[MODE_OF_OPERATION] = slaves[i]["outputs"]["mode_of_operation"].as<std::string>();
        if (slaves[i]["outputs"]["control_word"])
            slaveCfg[id].ecOutpMap[CONTROL_WORD] = slaves[i]["outputs"]["control_word"].as<std::string>();
        if (slaves[i]["outputs"]["target_position"])
            slaveCfg[id].ecOutpMap[TARGET_POSITION] = slaves[i]["outputs"]["target_position"].as<std::string>();
        if (slaves[i]["outputs"]["target_velocity"])
            slaveCfg[id].ecOutpMap[TARGET_VELOCITY] = slaves[i]["outputs"]["target_velocity"].as<std::string>();
        if (slaves[i]["outputs"]["target_torque"])
            slaveCfg[id].ecOutpMap[TARGET_TORQUE] = slaves[i]["outputs"]["target_torque"].as<std::string>();

    }

    if (isJntOK.size() != slave_number) {
        print_message("[YAML] Bad joint IDs. Please check the ID of each joint. ", MessageLevel::ERROR);
        return false;
    }

    return true;
}

bool EcatConfig::parserYamlFile() {
    return parserYamlFile(configFileName);
}

std::string EcatConfig::getEcInpVarName(int jntId, INPUTS enumEcInp) {
    return slaveCfg[jntId].name + "." + slaveCfg[jntId].ecInpMap[INPUT_GRP_NAME] + "." +
           slaveCfg[jntId].ecInpMap[enumEcInp];
}

std::string EcatConfig::getEcOutpVarName(int jntId, OUTPUTS enumEcOutp) {
    return slaveCfg[jntId].name + "." + slaveCfg[jntId].ecOutpMap[OUTPUT_GRP_NAME] + "." +
           slaveCfg[jntId].ecOutpMap[enumEcOutp];
}

bool EcatConfig::createSharedMemory() {
    mode_t mask = umask(0); // 取消屏蔽的权限位

    //////////////////// Semaphore //////////////////////////
    sem_mutex.resize(EC_SEM_NUM);
    for (int i = 0; i < EC_SEM_NUM; i++) {
        sem_mutex[i] = sem_open((EC_SEM_MUTEX + std::to_string(i)).c_str(), O_CREAT | O_RDWR, 0777, 1);
        if (sem_mutex[i] == SEM_FAILED) {
            print_message("[SHM] Can not open or create semaphore mutex " + std::to_string(i) + ".",
                          MessageLevel::ERROR);
            return false;
        }

        int val = 0;
        sem_getvalue(sem_mutex[i], &val);
//        std::cout << "value of sem_mutex is: " << val << std::endl;
        if (val != 1) {
            sem_destroy(sem_mutex[i]);
            sem_unlink((EC_SEM_MUTEX + std::to_string(i)).c_str());
            sem_mutex[i] = sem_open((EC_SEM_MUTEX + std::to_string(i)).c_str(), O_CREAT | O_RDWR, 0777, 1);
        }

        sem_getvalue(sem_mutex[i], &val);
        if (val != 1) {
            print_message("[SHM] Can not set semaphore mutex " + std::to_string(i) + " to value 1.",
                          MessageLevel::ERROR);
            return false;
        }

    }

    //////////////////// Shared Memory Object //////////////////////////
    using namespace boost::interprocess;
    shared_memory_object::remove(EC_SHM);

    managedSharedMemory = new managed_shared_memory{open_or_create, EC_SHM, EC_SHM_MAX_SIZE};

    ecatInfo = managedSharedMemory->find_or_construct<EcatInfo>("ecat")();
    ecatInfo->slave_number = slave_number;

    EcSlaveAlloc alloc_inst(managedSharedMemory->get_segment_manager());
    ecatSlaveVec = managedSharedMemory->find_or_construct<EcSlaveVec>("slaves")(slave_number, alloc_inst);

    CharAlloc char_alloc_inst(managedSharedMemory->get_segment_manager());
    StringAlloc string_alloc_inst(managedSharedMemory->get_segment_manager());
    ecatSlaveNameVec = managedSharedMemory->find_or_construct<EcStringVec>("slave_names")(slave_number,
                                                                                          EcString(char_alloc_inst),
                                                                                          string_alloc_inst);

//        ecatInfo->slaves.resize(1);
    print_message("OK!!!!.", MessageLevel::NORMAL);

    umask(mask); // 恢复umask的值

    return true;
}

bool EcatConfig::getSharedMemory() {

    mode_t mask = umask(0); // 取消屏蔽的权限位

    sem_mutex.resize(EC_SEM_NUM);
    for (int i = 0; i < EC_SEM_NUM; i++) {
        sem_mutex[i] = sem_open((EC_SEM_MUTEX + std::to_string(i)).c_str(), O_CREAT, 0777, 1);
        if (sem_mutex[i] == SEM_FAILED) {
            print_message("[SHM] Can not open or create semaphore mutex " + std::to_string(i) + ".",
                          MessageLevel::ERROR);
            return false;
        }
    }


    using namespace boost::interprocess;
    managedSharedMemory = new managed_shared_memory{open_or_create, EC_SHM, EC_SHM_MAX_SIZE};
    EcSlaveAlloc alloc_inst(managedSharedMemory->get_segment_manager());
    CharAlloc char_alloc_inst(managedSharedMemory->get_segment_manager());
    StringAlloc string_alloc_inst(managedSharedMemory->get_segment_manager());

    std::pair<EcatInfo *, std::size_t> p1 = managedSharedMemory->find<EcatInfo>("ecat");
    if (p1.first) {
        ecatInfo = p1.first;
    } else {
        print_message("[SHM] Ec-Master is not running.", MessageLevel::WARNING);
        ecatInfo = managedSharedMemory->construct<EcatInfo>("ecat")();
    }

    auto p2 = managedSharedMemory->find<EcSlaveVec>("slaves");
    if (p2.first) {
        ecatSlaveVec = p2.first;
    } else {
        print_message("[SHM] Ec-Master is not running.", MessageLevel::WARNING);
        ecatSlaveVec = managedSharedMemory->construct<EcSlaveVec>("slaves")(alloc_inst);
    }

    auto p3 = managedSharedMemory->find<EcStringVec>("slave_names");
    if (p3.first) {
        ecatSlaveNameVec = p3.first;
    } else {
        print_message("[SHM] Ec-Master is not running.", MessageLevel::WARNING);
        ecatSlaveNameVec = managedSharedMemory->construct<EcStringVec>("slave_names")(string_alloc_inst);
    }

    umask(mask); // 恢复umask的值

    return true;
}

std::string EcatConfig::to_string() {
    std::stringstream ss;
    for (int i = 0; i < ecatSlaveVec->size(); i++) {
        ss << "[Joint " << i << "] "
           << " stat: " << ecatSlaveVec->at(i).inputs.status_word << "; pos: "
           << ecatSlaveVec->at(i).inputs.position_actual_value
           << "; vel: " << ecatSlaveVec->at(i).inputs.velocity_actual_value << "; tor: "
           << ecatSlaveVec->at(i).inputs.torque_actual_value << ";";
    }
    return ss.str();
}

void EcatConfig::print_message(const std::string &msg, EcatConfig::MessageLevel msgLvl) {
    switch (msgLvl) {
        case MessageLevel::NORMAL:
            std::cout << _f % Color::GREEN << "[INFO]";
            break;
        case MessageLevel::WARNING:
            std::cout << _f % Color::YELLOW << "[WARNING]";
            break;
        case MessageLevel::ERROR:
            std::cout << _f % Color::RED << "[ERROR]";
            break;
        default:
            break;
    }

    std::cout << msg << _def << std::endl;
}

void EcatConfig::waitForSignal(int id) {
    sem_wait(sem_mutex[id]);
}

void EcatConfig::wait() {
    auto id = std::this_thread::get_id();
    auto it = std::find(threadId.begin(), threadId.end(), id);
    if(it != threadId.end()) { // thread is already in the list
        waitForSignal(std::distance(threadId.begin(), it));
    }
    else {
        if(threadId.size() >= EC_SEM_NUM) {
            print_message("[SHM] Too many threads.", MessageLevel::ERROR);
            return;
        }
        threadId.push_back(id);
        waitForSignal(threadId.size() - 1);
    }
}

void EcatConfig::init() {
    if (!getSharedMemory()) {
        print_message("[SHM] Can not get shared memory.", MessageLevel::ERROR);
        exit(1);
    }
    print_message("[SHM] Shared memory is ready.", MessageLevel::NORMAL);
}

