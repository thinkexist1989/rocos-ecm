//
// Created by Yang Luo on 3/27/23.
//

#include <ecat_config_master.h>


using namespace rocos;

EcatConfigMaster::EcatConfigMaster(int id) {
    ecmName = EC_SHM + std::to_string(id);
    mutexName = EC_SEM_MUTEX + std::to_string(id) + "_";
    pdInputName = "pd_input" + std::to_string(id);
    pdOutputName = "pd_output" + std::to_string(id);

}

EcatConfigMaster::~EcatConfigMaster() {

}

bool EcatConfigMaster::createSharedMemory() {
    mode_t mask = umask(0); // 取消屏蔽的权限位

    //////////////////// Shared Memory Object //////////////////////////
    using namespace boost::interprocess;
    shared_memory_object::remove(ecmName.c_str());

    managedSharedMemory = new managed_shared_memory{open_or_create, ecmName.c_str(), EC_SHM_MAX_SIZE};

    ecatBus = managedSharedMemory->find_or_construct<EcatBus>("ecat")();



    //////////////////// Semaphore //////////////////////////
    for (int i = 0; i < EC_SEM_NUM; i++) {
        sem_mutex[i] = sem_open((mutexName + std::to_string(i)).c_str(), O_CREAT | O_RDWR, 0777, 1);
        if (sem_mutex[i] == SEM_FAILED) {
            print_message("[SHM] Can not open or create semaphore mutex " + mutexName + std::to_string(i) + ".",
                          MessageLevel::ERROR);
            return false;
        }

        int val = 0;
        sem_getvalue(sem_mutex[i], &val);
//        std::cout << "value of sem_mutex is: " << val << std::endl;
        if (val != 1) {
            sem_destroy(sem_mutex[i]);
            sem_unlink((mutexName + std::to_string(i)).c_str());
            sem_mutex[i] = sem_open((mutexName + std::to_string(i)).c_str(), O_CREAT | O_RDWR, 0777, 1);
        }

        sem_getvalue(sem_mutex[i], &val);
        if (val != 1) {
            print_message("[SHM] Can not set semaphore mutex " + mutexName + std::to_string(i) + " to value 1.",
                          MessageLevel::ERROR);
            return false;
        }

    }


    umask(mask); // 恢复umask的值

    return true;
}

bool EcatConfigMaster::getSharedMemory() {

    getPdDataMemoryProvider();

    mode_t mask = umask(0); // 取消屏蔽的权限位

    for (int i = 0; i < EC_SEM_NUM; i++) {
        sem_mutex[i] = sem_open((mutexName + std::to_string(i)).c_str(), O_CREAT, 0777, 1);
        if (sem_mutex[i] == SEM_FAILED) {
            print_message("[SHM] Can not open or create semaphore mutex " + mutexName + std::to_string(i) + ".",
                          MessageLevel::ERROR);
            return false;
        }
    }

    using namespace boost::interprocess;
    managedSharedMemory = new managed_shared_memory{open_or_create, ecmName.c_str(), EC_SHM_MAX_SIZE};

    std::pair<EcatBus *, std::size_t> p1 = managedSharedMemory->find<EcatBus>("ecat");
    if (p1.first) {
        ecatBus = p1.first;
    } else {
        print_message("[SHM] Ec-Master is not running.", MessageLevel::WARNING);
        ecatBus = managedSharedMemory->construct<EcatBus>("ecat")();
    }

    umask(mask); // 恢复umask的值

    return true;
}

std::string EcatConfigMaster::to_string() {
    std::stringstream ss;

    return ss.str();
}

void EcatConfigMaster::print_message(const std::string &msg, EcatConfigMaster::MessageLevel msgLvl) {
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

void EcatConfigMaster::waitForSignal(int id) {
    sem_wait(sem_mutex[id]);
}

void EcatConfigMaster::wait() {
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

void EcatConfigMaster::init() {
    if (!getSharedMemory()) {
        print_message("[SHM] Can not get shared memory.", MessageLevel::ERROR);
        exit(1);
    }
    print_message("[SHM] Shared memory is ready.", MessageLevel::NORMAL);
}

bool EcatConfigMaster::createPdDataMemoryProvider(int pdInputSize, int pdOutputSize) {
    using namespace boost::interprocess;

    shared_memory_object::remove(pdInputName.c_str());
    shared_memory_object::remove(pdOutputName.c_str());

    pdInputShm = new shared_memory_object(open_or_create, pdInputName.c_str(), read_write);
    pdOutputShm = new shared_memory_object(open_or_create, pdOutputName.c_str(), read_write);

    pdInputShm->truncate(pdInputSize);
    pdOutputShm->truncate(pdOutputSize);

    pdInputRegion = new mapped_region(*pdInputShm, read_write);
    pdOutputRegion = new mapped_region(*pdOutputShm, read_write);

    pdInputPtr = static_cast<char *>(pdInputRegion->get_address());
    pdOutputPtr = static_cast<char *>(pdOutputRegion->get_address());

    return true;
}

bool EcatConfigMaster::getPdDataMemoryProvider() {
    using namespace boost::interprocess;

    pdInputShm = new shared_memory_object(open_or_create, pdInputName.c_str(), read_write);
    pdOutputShm = new shared_memory_object(open_or_create, pdOutputName.c_str(), read_write);

    pdInputRegion = new mapped_region(*pdInputShm, read_write);
    pdOutputRegion = new mapped_region(*pdOutputShm, read_write);


    pdInputPtr = static_cast<char *>(pdInputRegion->get_address());
    pdOutputPtr = static_cast<char *>(pdOutputRegion->get_address());

    return true;
}

void EcatConfigMaster::updateSempahore() {
    ////============== semphore update by think =================////
    // 通知其他进程可以更新这个周期的数据了 by think
    for (auto &sem: this->sem_mutex) {
        int val = 0;
        sem_getvalue(sem, &val);
        if (val < 1)
            sem_post(sem);
    }
}

