//
// Created by think on 2023/12/11.
//

#include <rocos_ecm/ecat_config.h>
#include <algorithm>
#include <iostream>


using namespace rocos;

EcatConfig::EcatConfig() {

}

EcatConfig::~EcatConfig() {

}

bool EcatConfig::getSharedMemory() {
    mode_t mask = umask(0); // 取消屏蔽的权限位


    getPdDataMemoryProvider();


    using namespace boost::interprocess;
    managedSharedMemory = new managed_shared_memory{open_or_create, EC_SHM, EC_SHM_MAX_SIZE};
//    managedSharedMemory = new managed_shared_memory{open_only, EC_SHM};

    std::pair<EcatBus *, std::size_t> p1 = managedSharedMemory->find<EcatBus>("ecat");
    if (p1.first) {
        ecatBus = p1.first;
    } else {
        print_message("[SHM] Ec-Master is not running.", MessageLevel::WARNING);
        ecatBus = managedSharedMemory->construct<EcatBus>("ecat")();
    }

    for (int i = 0; i < EC_SEM_NUM; i++) {
        sem_mutex[i] = sem_open((EC_SEM_MUTEX + std::to_string(i)).c_str(), O_CREAT, 0777, 1);
        if (sem_mutex[i] == SEM_FAILED) {
            print_message("[SHM] Can not open or create semaphore mutex " + std::to_string(i) + ".",
                          MessageLevel::ERROR);
            return false;
        }
    }

    umask(mask); // 恢复umask的值

    return true;
}

bool EcatConfig::getPdDataMemoryProvider() {
    using namespace boost::interprocess;

    pdInputShm = new shared_memory_object(open_or_create, "pd_input", read_write);
    pdOutputShm = new shared_memory_object(open_or_create, "pd_output", read_write);

    pdInputRegion = new mapped_region(*pdInputShm, read_write);
    pdOutputRegion = new mapped_region(*pdOutputShm, read_write);


    pdInputPtr = static_cast<char *>(pdInputRegion->get_address());
    pdOutputPtr = static_cast<char *>(pdOutputRegion->get_address());

    return true;
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
        print_message("[INIT] Can not get shared memory.", MessageLevel::ERROR);
        exit(1);
    }

    getPdDataMemoryProvider();

    print_message("[INIT] Ready!", MessageLevel::NORMAL);
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


