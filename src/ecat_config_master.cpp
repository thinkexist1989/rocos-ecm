//
// Created by Yang Luo on 3/27/23.
//

#include <rocos_ecm/ecat_config_master.h>


using namespace rocos;

EcatConfigMaster::EcatConfigMaster() {

}

EcatConfigMaster::~EcatConfigMaster() {

}

bool EcatConfigMaster::createSharedMemory() {
    mode_t mask = umask(0); // 取消屏蔽的权限位

    //////////////////// Shared Memory Object //////////////////////////
    using namespace boost::interprocess;
    shared_memory_object::remove(EC_SHM);

    managedSharedMemory = new managed_shared_memory{open_or_create, EC_SHM, EC_SHM_MAX_SIZE};
    // 创建共享内存对象ecatBus
    ecatBus = managedSharedMemory->find_or_construct<EcatBus>("ecat")();
    // 这部分代码使用 Boost Interprocess 库创建了一个共享内存对象。首先，通过 remove 移除任何已存在的同名共享内存对象。然后，使用 open_or_create 模式创建或打开指定名称（EC_SHM）的共享内存，
    // 最后通过 find_or_construct 方法查找或构造 EcatBus 类型的共享内存对象，该对象被命名为 "ecat"。


    //////////////////// Semaphore //////////////////////////
    for (int i = 0; i < EC_SEM_NUM; i++) {
        sem_mutex[i] = sem_open((EC_SEM_MUTEX + std::to_string(i)).c_str(), O_CREAT | O_RDWR, 0777, 1);
        // sem_open 是一个 POSIX 标准中定义的函数，用于创建或打开具有唯一名称的信号量。这个函数允许进程通过一个字符串名称来访问同一个信号量，从而实现不同进程之间的同步。
        // sem_t *sem_open(const char *name, int oflag, mode_t mode, unsigned int value);
        if (sem_mutex[i] == SEM_FAILED) {
            print_message("[SHM] Can not open or create semaphore mutex " + std::to_string(i) + ".",
                          MessageLevel::ERROR);
            return false;
        }

        int val = 0;
        sem_getvalue(sem_mutex[i], &val);
        // int sem_getvalue(sem_t *sem, int *sval);
        // sem_getvalue 函数会将信号量的当前值存储在 sval 指向的位置。如果信号量的值是正的，表示有资源可用；如果是零，表示没有资源可用；如果是负的，表示有等待的进程或线程。
//        std::cout << "value of sem_mutex is: " << val << std::endl;
        if (val != 1) {
            sem_destroy(sem_mutex[i]);
            sem_unlink((EC_SEM_MUTEX + std::to_string(i)).c_str());
            sem_mutex[i] = sem_open((EC_SEM_MUTEX + std::to_string(i)).c_str(), O_CREAT | O_RDWR, 0777, 1);
            // 代码首先获取了信号量的值，如果值不等于1，就销毁原有的信号量并重新创建一个初始值为1的信号量。这样的目的可能是确保信号量在使用之前处于一个已知的初始状态，即确保信号量的值始终为1。
        }

        sem_getvalue(sem_mutex[i], &val);
        if (val != 1) {
            print_message("[SHM] Can not set semaphore mutex " + std::to_string(i) + " to value 1.",
                          MessageLevel::ERROR);
            return false;
        }
        // 这一部分是为了再次确认新创建的信号量的值是否成功设置为1。如果不是1，就输出错误消息并返回 false，表示创建共享内存和信号量的过程出现了问题。


    }


    umask(mask); // 恢复umask的值

    return true;
}

bool EcatConfigMaster::getSharedMemory() {

    getPdDataMemoryProvider();

    mode_t mask = umask(0); // 取消屏蔽的权限位

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
    //  // 查找共享内存中是否存在名为 "ecat" 的 EcatBus 对象
    std::pair<EcatBus *, std::size_t> p1 = managedSharedMemory->find<EcatBus>("ecat");
    if (p1.first) {
        // 如果找到，说明 Ec-Master 已经在运行，直接使用找到的 EcatBus 对象
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
// 参考价值很大，以后的异常输出打印都可以参考这个函数
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
//     如果 sem_mutex[id] 的值大于零，表示资源可用，sem_wait 会将信号量的值减一，并继续执行后续代码。
// 如果 sem_mutex[id] 的值等于零，表示没有资源可用，线程（或进程）将被阻塞，直到信号量的值变为大于零。
// 如果信号量的值小于零，sem_wait 将出错。
}
// 这段代码的作用是管理线程的等待操作。如果线程已经在列表中，直接等待信号；如果不在列表中，检查是否可以添加到列表，
// 然后等待信号。这可能是为了控制多线程环境中的某些操作的执行顺序或者同步
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

    shared_memory_object::remove("pd_input");
    shared_memory_object::remove("pd_output");

    pdInputShm = new shared_memory_object(open_or_create, "pd_input", read_write);
    pdOutputShm = new shared_memory_object(open_or_create, "pd_output", read_write);
    // 设置共享内存对象的大小
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
    // 创建或打开共享内存对象，用于 PD 输入和输出
    pdInputShm = new shared_memory_object(open_or_create, "pd_input", read_write);
    pdOutputShm = new shared_memory_object(open_or_create, "pd_output", read_write);
    // 创建或打开共享内存的映射区域
    pdInputRegion = new mapped_region(*pdInputShm, read_write);
    pdOutputRegion = new mapped_region(*pdOutputShm, read_write);
    // 获取映射区域的起始地址，并将其转换为字符指针

    pdInputPtr = static_cast<char *>(pdInputRegion->get_address());
    pdOutputPtr = static_cast<char *>(pdOutputRegion->get_address());

    return true;
}

