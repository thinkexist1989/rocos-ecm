#ifndef EC_FLAGS_H
#define EC_FLAGS_H

#include "gflags/gflags.h"

//! @brief the path of EtherCAT configuration file(YAML). Default value is "/etc/rocos-ecm/ecat_config.yaml"
DECLARE_string(config);
//! @brief the path of eni file(EtherCAT Network Information). Default value is "/etc/rocos-ecm/eni.xml"
DECLARE_string(eni);
//! @brief the running duration of Ec-Master in msec, 0 is forever
DECLARE_int32(duration);
//! @brief Bus cycle time in usec. Default is 1000
DECLARE_int32(cycle);
//! @brief Verbosity level
DECLARE_int32(verbose);
//! @brief Which CPU the Ec-Master use
DECLARE_int32(cpuidx);
//! @brief Measurement in us for all EtherCAT jobs
DECLARE_bool(perf);
//! @brief Clock period in μs
DECLARE_int32(auxclk);
//! @brief The remote API Server port
DECLARE_int32(sp);
//! @brief Log file prefix
DECLARE_string(log);
//! @brief Flash outputs address.
DECLARE_int32(flash);
//! @brief DC mode
DECLARE_int32(dcmmode);
//! @brief Disable DCM control loop for diagnosis
DECLARE_bool(ctloff);

//! @brief Intel network card instances and mode
DECLARE_int32(link);
DECLARE_int32(instance);
DECLARE_int32(mode);


#endif // EC_FLAGS_H