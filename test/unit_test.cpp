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
Shenyang Institute of Automation, Chinese Academy of Sciences.
 email: luoyang@sia.cn

@Created on: 2021.11.29
*/

//#define DOCTEST_CONFIG_IMPLEMENT
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <test/doctest.h>

#include <rocos_ecm/ecat_config.h>
#include <iostream>

TEST_CASE("info") {
    rocos::EcatConfig ecatConfig;
    ecatConfig.init();

    std::cout << "Slave count: " << ecatConfig.ecatBus->slave_num << std::endl;

    std::cout << "Authorized: " << ecatConfig.isAuthorized() << std::endl;

    std::cout << "Min cycle time: " << ecatConfig.getBusMinCycleTime() << std::endl;
    std::cout << "Max cycle time: " << ecatConfig.getBusMaxCycleTime() << std::endl;
    std::cout << "Avg cycle time: " << ecatConfig.getBusAvgCycleTime() << std::endl;
    std::cout << "Curr cycle time: " << ecatConfig.getBusCurrentCycleTime() << std::endl;

    std::cout << ecatConfig.ecatBus->slaves[0].input_vars[0].name << std::endl;

    for(int i = 0; i < ecatConfig.ecatBus->slave_num; i++) {
        std::cout << "---------------------------------------------------------------" << std::endl;
        std::cout << "Slave name: " << ecatConfig.ecatBus->slaves[i].name << std::endl;
        std::cout << "  -input_count  : " << ecatConfig.ecatBus->slaves[i].input_var_num << std::endl;
        std::cout << "    -status_word: " << ecatConfig.findSlaveInputVarValueByName<uint16_t>(i, "Status word") << std::endl;
        std::cout << "    -pos_act_val: " << ecatConfig.findSlaveInputVarValueByName<int32_t>(i, "Position actual value") << std::endl;
        std::cout << "    -vel_act_val: " << ecatConfig.findSlaveInputVarValueByName<int32_t>(i, "Velocity actual value") << std::endl;
        std::cout << "    -tor_act_val: " << ecatConfig.findSlaveInputVarValueByName<int16_t>(i, "Torque actual value") << std::endl;
        std::cout << "  -output_count : " << ecatConfig.ecatBus->slaves[i].output_var_num << std::endl;
        std::cout << "    -ctrl_word  : " << ecatConfig.findSlaveOutputVarValueByName<uint16_t>(i, "Control word") << std::endl;
        std::cout << "    -mode_op    : " << (int)ecatConfig.findSlaveOutputVarValueByName<uint8_t>(i, "Mode of operation") << std::endl;
        std::cout << "    -tar_pos    : " << ecatConfig.findSlaveOutputVarValueByName<int32_t>(i, "Target Position") << std::endl;
        std::cout << "    -tar_vel    : " << ecatConfig.findSlaveOutputVarValueByName<int32_t>(i, "Target Velocity") << std::endl;
        std::cout << "    -tar_tor    : " << ecatConfig.findSlaveOutputVarValueByName<int16_t>(i, "Target Torque") << std::endl;
    }

}

TEST_CASE("reset cycle time") {
    rocos::EcatConfig ecatConfig;
    ecatConfig.init();

    std::cout << "Min cycle time: " << ecatConfig.getBusMinCycleTime() << std::endl;
    std::cout << "Max cycle time: " << ecatConfig.getBusMaxCycleTime() << std::endl;
    std::cout << "Avg cycle time: " << ecatConfig.getBusAvgCycleTime() << std::endl;
    std::cout << "Curr cycle time: " << ecatConfig.getBusCurrentCycleTime() << std::endl;


    ecatConfig.resetCycleTime();
    sleep(1);

    std::cout << "Min cycle time: " << ecatConfig.getBusMinCycleTime() << std::endl;
    std::cout << "Max cycle time: " << ecatConfig.getBusMaxCycleTime() << std::endl;
    std::cout << "Avg cycle time: " << ecatConfig.getBusAvgCycleTime() << std::endl;
    std::cout << "Curr cycle time: " << ecatConfig.getBusCurrentCycleTime() << std::endl;

}

TEST_CASE("sea_move") {
//    EcatConfigMaster ecatConfig;
//    ecatConfig.getSharedMemory();

//    int i = 0;
//    while(1) {
////        std::cout << "  Slave name:  " << ecatConfig.ecatSlaveNameVec->at(i) << std::endl;
//
//        std::cout << "Pos 1:  " << ecatConfig.ecatSlaveVec->at(i).inputs.position_actual_value << " ; Pos 2: "
//                  << ecatConfig.ecatSlaveVec->at(i).inputs.secondary_position_value << std::endl;
//        usleep(100000);
//    }

}

TEST_CASE("4 motor moving") {
    rocos::EcatConfig ecatConfig;
    ecatConfig.init();

    std::cout <<"Timestamp: " << ecatConfig.ecatBus->timestamp << std::endl;
    std::cout << "  Ethercat State: " << ecatConfig.ecatBus->current_state << std::endl;

    std::cout << "  " << ecatConfig.findSlaveInputVarValueByName<int32_t>(0, "Position actual value") << std::endl;
    std::cout << "  Slave number: " << ecatConfig.ecatBus->slave_num << std::endl;

    for(int i = 0; i < ecatConfig.ecatBus->slave_num; i++) {
        std::cout << "  Slave name:  " << ecatConfig.ecatBus->slaves[i].name << std::endl;
        *ecatConfig.findSlaveOutputVarPtrByName<uint8_t>(i, "Mode of operation") = 9;
        *ecatConfig.findSlaveOutputVarPtrByName<int16_t>(i, "Control word") = 128;

        *ecatConfig.findSlaveOutputVarPtrByName<int32_t>(i, "Target Position") = *ecatConfig.findSlaveInputVarPtrByName<int32_t>(i, "Position actual value");

        std::cout << "Pos is: " << ecatConfig.findSlaveInputVarValueByName<int32_t>(i, "Position actual value") << std::endl;
        usleep(10000);

        *ecatConfig.findSlaveOutputVarPtrByName<int16_t>(i, "Control word") = 6;
        usleep(10000);
        std::cout << "Status is:  " << *ecatConfig.findSlaveInputVarPtrByName<uint16_t>(i, "Status word") << std::endl;

        *ecatConfig.findSlaveOutputVarPtrByName<int16_t>(i, "Control word") = 7;
        usleep(10000);
        std::cout << "Status is:  " << *ecatConfig.findSlaveInputVarPtrByName<uint16_t>(i, "Status word") << std::endl;


        *ecatConfig.findSlaveOutputVarPtrByName<int16_t>(i, "Control word") = 15;
        *ecatConfig.findSlaveOutputVarPtrByName<int32_t>(i, "Target Velocity") = 100000;
        if(i == 3) {
            *ecatConfig.findSlaveOutputVarPtrByName<int32_t>(i, "Target Velocity") = 100000;
        }
        usleep(10000);
        std::cout << "Status is:  " << *ecatConfig.findSlaveInputVarPtrByName<uint16_t>(i, "Status word") << std::endl;

        usleep(5000000);

        *ecatConfig.findSlaveOutputVarPtrByName<int16_t>(i, "Control word") = 0;
        usleep(10000);
        std::cout << "Status is:  " << *ecatConfig.findSlaveInputVarPtrByName<uint16_t>(i, "Status word") << std::endl;
    }
}

TEST_CASE("csp") {
//    EcatConfig ecatConfig;
//    ecatConfig.getSharedMemory();
//    std::cout <<"Timestamp: " << ecatConfig.ecatInfo->timestamp << std::endl;
//    std::cout << "  Slave name:  " << ecatConfig.ecatSlaveNameVec->at(0) << std::endl;
//    std::cout << "  Ethercat State: " << ecatConfig.ecatInfo->ecatState << std::endl;
//    std::cout << "  " << *ecatConfig.ecatSlaveVec->at(0).inputs.position_actual_value << std::endl;
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.mode_of_operation = 8;
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 128;
//    *ecatConfig.ecatSlaveVec->at(0).outputs.target_position = *ecatConfig.ecatSlaveVec->at(0).inputs.position_actual_value ;
//    usleep(1000000);
//    std::cout << "Target Position is: " << *ecatConfig.ecatSlaveVec->at(0).outputs.target_position << std::endl;
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 6;
//    usleep(1000000);
//    std::cout << "Target Position is: " << *ecatConfig.ecatSlaveVec->at(0).outputs.target_position << std::endl;
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 7;
//    usleep(1000000);
//    std::cout << "Target Position is: " << *ecatConfig.ecatSlaveVec->at(0).outputs.target_position << std::endl;
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 15;
//    usleep(1000000);
//    std::cout << "Target Position is: " << *ecatConfig.ecatSlaveVec->at(0).outputs.target_position << std::endl;
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
////    ecatConfig.ecatSlaveVec->at(0).outputs.target_velocity = 10000;
//
//    usleep(10000000);
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 0;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
}

TEST_CASE("csv") {
//    EcatConfig ecatConfig;
//    ecatConfig.getSharedMemory();
//    std::cout <<"Timestamp: " << ecatConfig.ecatInfo->timestamp << std::endl;
//    std::cout << "  Slave name:  " << ecatConfig.ecatSlaveNameVec->at(0) << std::endl;
//    std::cout << "  Ethercat State: " << ecatConfig.ecatInfo->ecatState << std::endl;
//    std::cout << "  " << *ecatConfig.ecatSlaveVec->at(0).inputs.position_actual_value << std::endl;
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.mode_of_operation = 9;
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 128;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 6;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 7;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 15;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//    *ecatConfig.ecatSlaveVec->at(0).outputs.target_velocity = 10000;
//
//    usleep(10000000);
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 0;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
}

TEST_CASE("cst") {
//    EcatConfig ecatConfig;
//    ecatConfig.getSharedMemory();
//    std::cout <<"Timestamp: " << ecatConfig.ecatInfo->timestamp << std::endl;
//    std::cout << "  Slave name:  " << ecatConfig.ecatSlaveNameVec->at(0) << std::endl;
//    std::cout << "  Ethercat State: " << ecatConfig.ecatInfo->ecatState << std::endl;
//    std::cout << "  " << *ecatConfig.ecatSlaveVec->at(0).inputs.position_actual_value << std::endl;
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.mode_of_operation = 10;
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 128;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 6;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 7;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 15;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
//    *ecatConfig.ecatSlaveVec->at(0).outputs.target_torque = 70;
//
//    usleep(20000000);
//
//    *ecatConfig.ecatSlaveVec->at(0).outputs.control_word = 0;
//    usleep(1000000);
//    std::cout << "Status is:  " << *ecatConfig.ecatSlaveVec->at(0).inputs.status_word << std::endl;
}

TEST_CASE("Cycling Time Print") {
//    EcatConfig ecatConfig;
//    ecatConfig.getSharedMemory();
//
//    for(int i = 0; i < 20; i++) {
//        std::cout <<"Timestamp: " << ecatConfig.ecatInfo->timestamp << std::endl;
//        std::cout << "  Slave name:  " << ecatConfig.ecatSlaveNameVec->at(0) << std::endl;
//        std::cout << "  Ethercat State: " << ecatConfig.ecatInfo->ecatState << std::endl;
//
//        std::cout << "  Current cycling Time: " << ecatConfig.ecatInfo->currCycleTime  << std::endl;
//        std::cout << "  Min cycling Time: " << ecatConfig.ecatInfo->minCycleTime  << std::endl;
//        std::cout << "  Max cycling Time: " << ecatConfig.ecatInfo->maxCycleTime  << std::endl;
//        std::cout << "  Avg cycling Time: " << ecatConfig.ecatInfo->avgCycleTime << std::endl;
//
//        usleep(1000000);
//    }


}


//int main(int argc, char** argv) {
//    doctest::Context context;
//
//    // !!! THIS IS JUST AN EXAMPLE SHOWING HOW DEFAULTS/OVERRIDES ARE SET !!!
//
//    // defaults
//    context.addFilter("test-case-exclude", "*math*"); // exclude test cases with "math" in their name
//    context.setOption("abort-after", 5);              // stop test execution after 5 failed assertions
//    context.setOption("order-by", "name");            // sort the test cases by their name
//
//    context.applyCommandLine(argc, argv);
//
//    // overrides
//    context.setOption("no-breaks", true);             // don't break in the debugger when assertions fail
//
//    //---------------------八室初始化----------------------------
////    int err = 0;
////    command_arg arg;
////    int ac = 3;
////    char av1[] = "--EtherCATonly"; char av2[] = "on";
////    char* av[3] = {argv[0], av1 , av2};
////    err = commandLineParser(argc, av, &arg);
////    if (0 != err) {
////        return -1;
////    }
////    err = system_initialize(&arg);
////
////    if (0 != err) {
////        return err;
////    }
//    //--------------------八室初始化完毕------------------------
//
//    int res = context.run(); // run
//
//    if(context.shouldExit()) // important - query flags (and --exit) rely on the user doing this
//        return res;          // propagate the result of the tests
//
//    int client_stuff_return_code = 0;
//    // your program - if the testing framework is integrated in your production code
//
//    return res + client_stuff_return_code; // the result from doctest is propagated here as well
//}
