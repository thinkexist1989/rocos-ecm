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
@Last Modified: 2023.4.4 18:00
*/
#ifndef EC_FLAGS_H
#define EC_FLAGS_H

#include "gflags/gflags.h"

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

DECLARE_string(state);

//! @brief license
DECLARE_string(license);


#endif // EC_FLAGS_H