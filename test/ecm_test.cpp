#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <test/doctest.h>

#define private public
#define protected public

#include <rocos_ecm/ecat_config.h>
#include <iostream>
TEST_CASE("info")
{

    auto ecatConfig = rocos::EcatConfig::getInstance(1);

    std::cout << "Slave count: " << ecatConfig->ecatBus->slave_num << std::endl;

    std::cout << "Authorized: " << ecatConfig->isAuthorized() << std::endl;

    std::cout << "Min cycle time: " << ecatConfig->getBusMinCycleTime() << std::endl;
    std::cout << "Max cycle time: " << ecatConfig->getBusMaxCycleTime() << std::endl;
    std::cout << "Avg cycle time: " << ecatConfig->getBusAvgCycleTime() << std::endl;
    std::cout << "Curr cycle time: " << ecatConfig->getBusCurrentCycleTime() << std::endl;

    std::cout << ecatConfig->ecatBus->slaves[0].input_vars[0].name << std::endl;

    while (1)
    {

        for (int i = 0; i < ecatConfig->ecatBus->slave_num; i++)
        {
            std::cout << "---------------------------------------------------------------" << std::endl;
            std::cout << "Slave name: " << ecatConfig->ecatBus->slaves[i].name << std::endl;
            std::cout << "  -input_count  : " << ecatConfig->ecatBus->slaves[i].input_var_num << std::endl;
            std::cout << "    -status_word: " << ecatConfig->getSlaveInputVarValueByName<uint16_t>(i, "Statusword") << std::endl;
            std::cout << "    -pos_act_val: " << ecatConfig->getSlaveInputVarValueByName<int32_t>(i, "Position actual value") << std::endl;
            std::cout << "    -vel_act_val: " << ecatConfig->getSlaveInputVarValueByName<int32_t>(i, "Velocity actual value") << std::endl;
            std::cout << "    -tor_act_val: " << ecatConfig->getSlaveInputVarValueByName<int16_t>(i, "Torque actual value") << std::endl;
            std::cout << "  -output_count : " << ecatConfig->ecatBus->slaves[i].output_var_num << std::endl;
            std::cout << "    -ctrl_word  : " << ecatConfig->getSlaveOutputVarValueByName<uint16_t>(i, "Controlword") << std::endl;
            std::cout << "    -mode_op    : " << (int)ecatConfig->getSlaveOutputVarValueByName<uint8_t>(i, "Modes of operation") << std::endl;
            std::cout << "    -tar_pos    : " << ecatConfig->getSlaveOutputVarValueByName<int32_t>(i, "Target position") << std::endl;
            std::cout << "    -tar_vel    : " << ecatConfig->getSlaveOutputVarValueByName<int32_t>(i, "Target velocity") << std::endl;
            std::cout << "    -tar_tor    : " << ecatConfig->getSlaveOutputVarValueByName<int16_t>(i, "Target torque") << std::endl;

            ecatConfig->setSlaveOutputVarValueByName<uint8_t>(i, "Modes of operation", 9);
            ecatConfig->setSlaveOutputVarValueByName<uint16_t>(i, "Controlword", 128);
            usleep(100000);

            ecatConfig->setSlaveOutputVarValueByName<uint16_t>(i, "Controlword", 6);
            usleep(100000);

            ecatConfig->setSlaveOutputVarValueByName<uint16_t>(i, "Controlword", 7);
            usleep(100000);

            ecatConfig->setSlaveOutputVarValueByName<uint16_t>(i, "Controlword", 15);
            usleep(100000);

            ecatConfig->setSlaveOutputVarValueByName<int32_t>(i, "Target velocity", 10000);
            usleep(3000000);

            ecatConfig->setSlaveOutputVarValueByName<int32_t>(i, "Target velocity", 0);
            usleep(3000000);

            ecatConfig->setSlaveOutputVarValueByName<int32_t>(i, "Target velocity", -10000);
            usleep(3000000);

            ecatConfig->setSlaveOutputVarValueByName<int32_t>(i, "Target velocity", 0);
            usleep(3000000);

            ecatConfig->setSlaveOutputVarValueByName<uint16_t>(i, "Controlword", 0);
            usleep(100000);
        }
    }
}
