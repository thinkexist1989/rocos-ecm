/*-----------------------------------------------------------------------------
 * DCDemoMain.cpp
 * Copyright                acontis technologies GmbH, Weingarten, Germany
 * Response                 Stefan Zintgraf
 * Description              rocos_ecm main entrypoint
 * Modified                 Yang Luo , luoyang@sia.cn
 * Last Modification Date   2023.04.04 18:00
 *---------------------------------------------------------------------------*/

/*-INCLUDES------------------------------------------------------------------*/
#include "Logging.h"

#include "ecatProcess.h"
#include "selectLinkLayer.h"

#include "EcFlags.h" //!< gflags definition
#include "ver.h"

#if (defined ATEMRAS_SERVER)

#include "AtEmRasSrv.h"

#endif

#include <sys/mman.h>
#include <sys/utsname.h>
#include <csignal>


#include <cstdio>
#include <cstdlib>

//#include <sys/syscall.h>
#include <cstring>
#include <iostream>
#include <termcolor/termcolor.hpp>


/*-DEFINES-------------------------------------------------------------------*/
#define COMMAND_LINE_BUFFER_LENGTH 512

#define AUXCLOCK_SUPPORTED

/*-TYPEDEFS------------------------------------------------------------------*/
typedef struct {
    EC_T_VOID *pvTimingEvent;      /* event handle */
    EC_T_DWORD dwBusCycleTimeUsec; /* cycle time in usec */
    EC_T_BOOL bShutdown;          /* EC_TRUE if aux thread shall shut down */
    EC_T_BOOL bIsRunning;         /* EC_TRUE if the aux thread is running */
    EC_T_DWORD dwCpuIndex;         /* SMP systems: CPU index */

    EC_T_BOOL bUseAuxClock;       /* Either connect to IRQ or use sleep */
    EC_T_VOID *pvAuxClkEvent;      /* event handle */

} EC_T_TIMING_DESC;

/*-LOCAL FUNCTIONS-----------------------------------------------------------*/
//! \brief  signal handler.
//! \return N/A
static void SignalHandler(int nSignal) {
    bRun = EC_FALSE;
}

//! @brief Enable real-time environment
//! @return EC_E_NOERROR in case of success, EC_E_ERROR in case of failure.
EC_T_DWORD EnableRealtimeEnvironment(EC_T_VOID) {
    struct utsname SystemName;
    int nMaj, nMin, nSub;
    struct timespec ts;
    int nRetval;
    EC_T_DWORD dwResult = EC_E_ERROR;
    EC_T_BOOL bHighResTimerAvail;
    struct sched_param schedParam;

    /* master only tested on >= 2.6 kernel */
    nRetval = uname(&SystemName);
    if (nRetval != 0) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR calling uname(), required Linux kernel >= 2.6\n"));
        dwResult = EC_E_ERROR;
        goto Exit;
    }

//    sscanf(SystemName.release, "%d.%d.%d", &nMaj, &nMin, &nSub); // clang推荐用strtol来替代sscanf
    char *end;
    nMaj = (int) strtol(SystemName.release, &end, 10);
    nMin = (int) strtol(end + 1, &end, 10);
    nSub = (int) strtol(end + 1, &end, 10);

    if (!(((nMaj == 2) && (nMin == 6)) || (nMaj >= 3))) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR - detected kernel = %d.%d.%d, required Linux kernel >= 2.6\n", nMaj, nMin, nSub));
        dwResult = EC_E_ERROR;
        goto Exit;
    }

    /* request realtime scheduling for the current process
     * This value is overwritten for each individual task
     */
    schedParam.sched_priority = MAIN_THREAD_PRIO; /* 1 lowest priority, 99 highest priority */
    nRetval = sched_setscheduler(0, SCHED_FIFO, &schedParam);
    if (nRetval == -1) {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR - cannot change scheduling policy!\n"
                                                                         "root privilege is required or realtime group has to be joined!\n"));
        goto Exit;
    }

    /* disable paging */
    nRetval = mlockall(MCL_CURRENT | MCL_FUTURE);
    if (nRetval == -1) {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR - cannot disable paging!\n"));
        dwResult = EC_E_ERROR;
        goto Exit;
    }

    /* check if high resolution timers are available */
    if (clock_getres(CLOCK_MONOTONIC, &ts)) {
        bHighResTimerAvail = EC_FALSE;
    } else {
        bHighResTimerAvail = !(ts.tv_sec != 0 || ts.tv_nsec != 1);
    }
    if (!bHighResTimerAvail) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "WARNING: High resolution timers not available\n"));
    }

    /* set type of OsSleep implementation  (eSLEEP_USLEEP, eSLEEP_NANOSLEEP or eSLEEP_CLOCK_NANOSLEEP) */
    OsSleepSetType(eSLEEP_CLOCK_NANOSLEEP);

    dwResult = EC_E_NOERROR;
    Exit:
    return dwResult;
}

//! \brief Show syntax
//!  \return: N/A
static EC_T_VOID ShowSyntax(EC_T_VOID) {
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Syntax:\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "EcMasterDemoDc [-f ENI-FileName] [-t time] [-b time] [-a affinity] [-v lvl] [-perf] [-log Prefix]"));
#if (defined AUXCLOCK_SUPPORTED)
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, " [-auxclk period]"));
#endif
#if (defined ATEMRAS_SERVER)
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, " [-sp [port]]"));
#endif
#if (defined VLAN_FRAME_SUPPORT)
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, " [-vlan id priority]"));
#endif
    ShowLinkLayerSyntax1();
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -f                Use given ENI file\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     FileName        file name .xml\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -t                Demo duration\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "     time            Time in msec, 0 = forever (default = 120000)\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -b                Bus cycle time\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     cycle time      Cycle time in usec\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -a                CPU affinity\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "     affinity        0 = first CPU, 1 = second, ...\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -v                Set verbosity level\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "     lvl             Level: 0=off, 1...n=more messages, 3(default) generate dcmlog file\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -perf             Enable job measurement\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -log              Use given file name prefix for log files\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     Prefix          prefix\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -flash            Flash outputs\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     address         0=all, >0 = slave address"));
#if (defined AUXCLOCK_SUPPORTED)
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -auxclk           use auxiliary clock\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     period          clock period in usec\n"));
#endif
#if (defined ATEMRAS_SERVER)
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -sp               Server port binding\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "     port            port (default = %d)\n", ATEMRAS_DEFAULT_PORT));
#endif
#if (defined VLAN_FRAME_SUPPORT) //这个定义在EcFeatures.h中，默认关闭，暂时不清楚用途 by think 2023.4.1 21:22
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -vlan             use VLAN\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     id              id (0...4095)\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     priority        priority (0...7)\n"));
#endif
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -dcmmode          Set DCM mode\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     off                Off\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "     busshift           BusShift mode (default)\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     mastershift        MasterShift mode\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "     masterrefclock     MasterRefClock mode\n"));
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "     linklayerrefclock  LinkLayerRefClock mode\n"));
#if (defined INCLUDE_DCX)
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "     dcx                External synchronization mode\n"));
#endif
    EcLogMsg(EC_LOG_LEVEL_ERROR,
             (pEcLogContext, EC_LOG_LEVEL_ERROR, "   -ctloff           Disable DCM control loop for diagnosis\n"));

    ShowLinkLayerSyntax2();
}

/// \brief Set event according to periodical sleep or aux clock
/// Cyclically sets an event for thread synchronization purposes.
/// Either use OsSleep() or use the aux clock by means of:
/// - Enable AUX clock if selected.
/// - Wait for IRQ, acknowledge IRQ, SetEvent in loop until shutdown
/// - Disable AUX clock
/// Return: N/A
static EC_T_VOID tEcTimingTask(EC_T_VOID *pvThreadParamDesc) {
    auto *pTimingDesc = (EC_T_TIMING_DESC *) pvThreadParamDesc;
    EC_T_CPUSET CpuSet;

    EC_CPUSET_ZERO(CpuSet);
    EC_CPUSET_SET(CpuSet, pTimingDesc->dwCpuIndex);
    OsSetThreadAffinity(EC_NULL, CpuSet);

    /* timing task started */
    pTimingDesc->bIsRunning = EC_TRUE;

    /* periodically generate events as long as the application runs */
    while (!pTimingDesc->bShutdown) {
        /* wait for the next cycle */

        {
            /* wait for next cycle (no cycle below 1ms) */
            OsSleep(EC_MAX(pTimingDesc->dwBusCycleTimeUsec / 1000, 1));
        }

        /* trigger jobtask */
        OsSetEvent(pTimingDesc->pvTimingEvent);
    }


    pTimingDesc->bIsRunning = EC_FALSE;

}

//! \brief  The application entry point.
//! \return  Value 0 is returned.
int main(int nArgc, char *ppArgv[]) {
    /// gflags parsing parameters
    gflags::SetVersionString(ROCOS_ECM_VERSION);
    gflags::ParseCommandLineFlags(&nArgc, &ppArgv, true);

    /// Print the configuration information
    std::cout << termcolor::on_bright_blue << termcolor::blink
              << "=================ROCOS EtherCAT Master=================" << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "==                  Author: Yang Luo                 ==" << termcolor::reset
              << std::endl;
    std::cout << termcolor::blue << "==                  Email: yluo@hit.edu.cn           ==" << termcolor::reset
              << std::endl;
    std::cout << termcolor::blue << "==                  Version: " << ROCOS_ECM_VERSION << "                   =="
              << termcolor::reset << std::endl;

    std::cout << termcolor::blue << "== Configuration file(-config): " << termcolor::reset << termcolor::bold
              << FLAGS_config << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== EtherCAT network information file(-eni): " << termcolor::reset
              << termcolor::bold << FLAGS_eni << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Performance measure: " << termcolor::reset << termcolor::bold
              << (FLAGS_perf ? "true" : "false") << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Running duration(ms): " << termcolor::reset << termcolor::bold
              << (FLAGS_duration ? std::to_string(FLAGS_duration) : "forever") << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Bus cycle time(us): " << termcolor::reset << termcolor::bold << FLAGS_cycle
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Verbose level: " << termcolor::reset << termcolor::bold << FLAGS_verbose
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== CPU index: " << termcolor::reset << termcolor::bold << FLAGS_cpuidx
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Clock period(us): " << termcolor::reset << termcolor::bold
              << (FLAGS_auxclk ? std::to_string(FLAGS_auxclk) : "disabled") << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Remote API server port: " << termcolor::reset << termcolor::bold << FLAGS_sp
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== link layer: " << termcolor::reset << termcolor::bold << FLAGS_link
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== instance: " << termcolor::reset << termcolor::bold << FLAGS_instance
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== mode: " << termcolor::reset << termcolor::bold << FLAGS_mode << termcolor::reset
              << std::endl;
    std::cout << termcolor::blue << "== Log file prefix: " << termcolor::reset << termcolor::bold << FLAGS_log
              << termcolor::reset << std::endl;

    std::cout << termcolor::on_bright_blue << termcolor::blink
              << "=======================================================" << termcolor::reset << std::endl;


    /// Variables Definition
    EC_T_CHAR szConfigFilename[256] = {'\0'}; // Config File Name(ecat_config.yaml)
    EC_T_PBYTE pbyCnf = nullptr;

    int nRetVal = APP_ERROR;
    EC_T_DWORD dwRes = EC_E_ERROR;
    EC_T_BOOL bLogInitialized = EC_FALSE;
    EC_T_CHAR szCommandLine[COMMAND_LINE_BUFFER_LENGTH];
    EC_T_CHAR szFullCommandLine[COMMAND_LINE_BUFFER_LENGTH];
    EC_T_BOOL bGetNextWord = EC_TRUE;
    EC_T_CHAR *ptcWord = EC_NULL;
    EC_T_CHAR tcStorage = '\0';

    EC_T_CHAR szLogFilePrefix[256] = {'\0'};
    EC_T_CNF_TYPE eCnfType = eCnfType_Unknown;
    EC_T_PBYTE pbyEni = nullptr;
    EC_T_DWORD dwCnfDataLen = 0;
    EC_T_CHAR szENIFilename[256] = {'\0'};
    EC_T_DWORD dwDuration = 120000;
    EC_T_DWORD dwNumLinkLayer = 0;
    EC_T_LINK_PARMS *apLinkParms[MAX_LINKLAYER];
#ifdef ATEMRAS_SERVER
    EC_T_WORD wServerPort = 0xFFFF;
#endif
    CAtEmLogging oLogging;
    EC_T_DWORD dwCpuIndex = 0;
    EC_T_CPUSET CpuSet;
    EC_T_BOOL bEnaPerfJobs = EC_FALSE;  /* enable job measurements （在1140行，如果有-perf参数，就EC_TRUE）*/
    EC_T_INT nFlashAddress = 0xFFFF;
    EC_T_TIMING_DESC TimingDesc;
    EC_T_BOOL bStartTimingTask = EC_FALSE;
    EC_T_INT nVerbose = 3;
#if (defined VLAN_FRAME_SUPPORT)
    EC_T_BOOL               bVLANEnable         = EC_FALSE;
    EC_T_WORD               wVLANId             = 0;
    EC_T_BYTE               byVLANPrio          = 0;
#endif
#if defined(INCLUDE_TTS)
    EC_T_VOID* pvTtsCycleEvent = EC_NULL;
#endif

#if (defined UNDER_RTSS)
    HANDLE                  hTimer              = NULL;
    LARGE_INTEGER           liTimer;
#endif

#if !(defined EC_VERSION_UC3)
    EC_T_OS_PARMS oOsParms;
    EC_T_OS_PARMS *pOsParms = &oOsParms;
#endif
    EC_T_DCM_MODE eDcmMode = eDcmMode_BusShift;
    EC_T_BOOL bCtlOff = EC_FALSE;

    /* printf logging until logging initialized */
    G_pEcLogParms->pfLogMsg = CAtEmLogging::LogMsgOsPrintf;
    G_pEcLogParms->dwLogLevel = EC_LOG_LEVEL_UNDEFINED;

    OsMemset(apLinkParms, 0, sizeof(EC_T_LINK_PARMS *) * MAX_LINKLAYER);
    OsMemset(&TimingDesc, 0, sizeof(EC_T_TIMING_DESC));
#if !(defined EC_VERSION_UC3)
    OsMemset(pOsParms, 0, sizeof(EC_T_OS_PARMS));
    pOsParms->dwSignature = EC_OS_PARMS_SIGNATURE;
    pOsParms->dwSize = sizeof(EC_T_OS_PARMS);
    pOsParms->dwSupportedFeatures = 0xFFFFFFFF;
#endif
    szCommandLine[0] = '\0';

    /// OS specific initialization
#if (defined LINUX)
    dwRes = EnableRealtimeEnvironment(); // Enabling Realtime Env by think 2023.4.1 21:29
    if (EC_E_NOERROR != dwRes) {
        goto Exit;
    }
    {
        sigset_t SigSet;
        int nSigNum = SIGALRM;
        sigemptyset(&SigSet);
        sigaddset(&SigSet, nSigNum);
        sigprocmask(SIG_BLOCK, &SigSet, NULL);
        signal(SIGINT, SignalHandler);
        signal(SIGTERM, SignalHandler);
    }
#endif /* LINUX */

#if !(defined UNDER_RTSS)
    /* Seed the random-number generator with current time so that
     * the numbers will be different every time we run.
     */
    srand((unsigned) OsQueryMsecCount());
#endif


    /* set running flag */
    bRun = EC_TRUE; // 定义在ecatProcessCommon.cpp中的全局变量，在ecatProcess.cpp中while判断用到

    /* Initialize Timing Event descriptor */
    TimingDesc.bShutdown = EC_FALSE;
    TimingDesc.bIsRunning = EC_FALSE;
    TimingDesc.dwBusCycleTimeUsec = CYCLE_TIME * 1000; //在1415行，如果-b参数，就TimingDesc.dwBusCycleTimeUsec设置为相应值


    //! Process commandline arguments
    {
        // -config
        OsSnprintf(szConfigFilename, sizeof(szConfigFilename) - 1, "%s",
                   FLAGS_config.c_str()); // 将config文件名保存到szConfigFilename中

        pbyCnf = (EC_T_PBYTE) &szConfigFilename[0];

        // -eni
        OsSnprintf(szENIFilename, sizeof(szENIFilename) - 1, "%s", FLAGS_eni.c_str()); //将eni文件名保存到szENIFilename中

        // -duration
        dwDuration = FLAGS_duration; // 将duration保存到dwDuration中

        // -cycle
#if (defined AUXCLOCK_SUPPORTED)
        if (TimingDesc.bUseAuxClock) {
            EcLogMsg(EC_LOG_LEVEL_INFO,
                     (pEcLogContext, EC_LOG_LEVEL_INFO, "Using bus cycle time %d usec from auxclock parameter\n", TimingDesc.dwBusCycleTimeUsec));
        } else
#endif
        {
            TimingDesc.dwBusCycleTimeUsec = FLAGS_cycle; // 将cycle保存到TimingDesc.dwBusCycleTimeUsec中
        }

        // -verbose
        nVerbose = FLAGS_verbose; // 将verbose保存到nVerbose中

        // -cpuidx
        dwCpuIndex = FLAGS_cpuidx; // 将cpuidx保存到dwCpuIndex中

        // -perf
        bEnaPerfJobs = FLAGS_perf; // 将perf保存到bEnaPerfJobs中

        // -auxclk
#if (defined AUXCLOCK_SUPPORTED)
        if (FLAGS_auxclk != 0) {
            TimingDesc.bUseAuxClock = EC_TRUE;
            TimingDesc.dwBusCycleTimeUsec = FLAGS_auxclk; // 将auxclk保存到TimingDesc.dwBusCycleTimeUsec中
            if (TimingDesc.dwBusCycleTimeUsec < 10) {
                TimingDesc.dwBusCycleTimeUsec = 10;
            }
        } else {
            TimingDesc.bUseAuxClock = EC_FALSE;
        }
#else
        EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Auxiliary clock not supported by this operating system!)\n"));
                goto Exit;
#endif

        // -sp
#if (defined ATEMRAS_SERVER)
        wServerPort = FLAGS_sp; // 将sp保存到wServerPort中
#endif

        // -log
        OsSnprintf(szLogFilePrefix, sizeof(szLogFilePrefix) - 1, "%s", FLAGS_log.c_str()); // 将prefix保存到szLogFilePrefix中

        // -flash
        nFlashAddress = FLAGS_flash; // 将flash保存到nFlashAddress中

        // -dcmmode
        switch (FLAGS_dcmmode) {
            case 0:
                eDcmMode = eDcmMode_Off;
                break;
            case 1:
                eDcmMode = eDcmMode_BusShift;
                break;
            case 2:
                eDcmMode = eDcmMode_MasterShift;
                break;
            case 3:
                eDcmMode = eDcmMode_LinkLayerRefClock;
                break;
            case 4:
                eDcmMode = eDcmMode_MasterRefClock;
                break;
#if (defined INCLUDE_DCX)
            case 5:
                eDcmMode = eDcmMode_Dcx;
                break;
#endif
            default:
                break;
        }

        // -ctloff
        bCtlOff = FLAGS_ctloff; // 将ctloff保存到bCtloff中

        // link layer process

        EC_T_CHAR *szNextParm = ptcWord;
        EC_T_DWORD dwNewCycleDurationUsec = 0;

        auto instance = std::strtol(FLAGS_instance.c_str(), nullptr, 0);

        dwRes = CreateLinkParms(FLAGS_link, instance, FLAGS_mode, &apLinkParms[dwNumLinkLayer]);
        if (EC_E_NOERROR != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "SYNTAX_ERROR: %s!\n", szNextParm));
            nRetVal = SYNTAX_ERROR;
            goto Exit;
        }

        if (dwNumLinkLayer > 1) {
            nRetVal = SYNTAX_ERROR;
            goto Exit;
        } else {
            EC_CPUSET_ZERO(CpuSet);
            EC_CPUSET_SET(CpuSet, dwCpuIndex);
            apLinkParms[dwNumLinkLayer]->dwIstPriority = (CpuSet << 16) | RECV_THREAD_PRIO;

            dwNumLinkLayer++;

            if (dwNewCycleDurationUsec != 0) {
                TimingDesc.dwBusCycleTimeUsec = dwNewCycleDurationUsec;
            }
        }

    }



//    /* prepare command line */
//
//    /* build szCommandLine from argument list */
//    {
//        EC_T_CHAR *pcStrCur = szCommandLine;
//        EC_T_INT nStrRemain = COMMAND_LINE_BUFFER_LENGTH;
//
//        EC_T_CHAR szStrFormat[] = "%s";
//
//        /* build szCommandLine from argument list, skipping executable name */
//        for (nArgc--, ppArgv++; nArgc > 0; nArgc--, ppArgv++) {
//            EC_T_BOOL bIsFileName = EC_FALSE;
//
//            /* insert next argument */
//            OsSnprintf(pcStrCur, nStrRemain - 1, szStrFormat, *ppArgv);
//
//            /* check for file name */
//            if (0 == OsStrcmp(pcStrCur, "-f")) {
//                bIsFileName = EC_TRUE;
//            }
//            /* adjust string cursor */
//            nStrRemain -= (EC_T_INT) OsStrlen(pcStrCur);
//            pcStrCur = pcStrCur + OsStrlen(pcStrCur);
//
//            /* insert space */
//            OsStrncpy(pcStrCur, " ", nStrRemain - 1);
//            nStrRemain--;
//            pcStrCur++;
//
//            if (bIsFileName && (1 < nArgc)) {
//                /* move to next arg (ENI file name) */
//                nArgc--;
//                ppArgv++;
//
//                /* insert quotation mark */
//                OsStrncpy(pcStrCur, "\"", nStrRemain - 1);
//                nStrRemain--;
//                pcStrCur++;
//
//                /* insert ENI file name */
//                OsSnprintf(pcStrCur, nStrRemain - 1, szStrFormat, *ppArgv);
//                nStrRemain -= (EC_T_INT) OsStrlen(pcStrCur);
//                pcStrCur = pcStrCur + OsStrlen(pcStrCur);
//
//                /* insert quotation mark */
//                OsStrncpy(pcStrCur, "\" ", nStrRemain - 1);
//                nStrRemain--;
//                pcStrCur++;
//            }
//        }
//    }
//    /* backup full command line */
//    OsStrncpy(szFullCommandLine, szCommandLine, sizeof(szFullCommandLine) - 1);
//
//    /* parse command line */
//    for (ptcWord = OsStrtok(szCommandLine, " ");
//         ptcWord != EC_NULL;) { // OsStrtok函数其实就是strtok函数，其将szCommandLine按照第二个参数拆分为多个字符串，同时返回第一个字符串指针，原字符串被修改
//        if (0 == OsStricmp(ptcWord, "-f")) { // OsStricmp函数其实就是stricmp函数，其比较两个字符串是否相等，不区分大小写
//            EC_T_INT nPtcWordIndex = 3; // 3是因为-f后面有一个空格
//
//            /* Search for the start of the config file name. The config file
//               name may start with quotation marks because of blanks in the filename */
//            while (ptcWord[nPtcWordIndex] != '\0') {
//                if ((ptcWord[nPtcWordIndex] == '\"') ||
//                    (ptcWord[nPtcWordIndex] != ' ')) { //只要“不是空格”或者“是双引号”，说明找到了文件名的开始，跳出循环
//                    break;
//                }
//                nPtcWordIndex++;
//            }
//
//            /* Depending of a config file name within quotation marks (or without
//               quotation marks) extract the filename */
//            if (ptcWord[nPtcWordIndex] == '\"') { //如果是双引号，说明文件名是在双引号中的
//                /* Check if the strtok position is already correct */
//                if (nPtcWordIndex > 3) {
//                    /* More than 1 blank before -f. Correct strtok position. */
//                    OsStrtok(EC_NULL, "\"");
//                }
//
//                /* Now extract the config file name */
//                ptcWord = OsStrtok(EC_NULL, "\"");
//            } else { //如果不是双引号，说明文件名是没有双引号的
//                /* Extract the config file name if it was not set within quotation marks */
//                ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//            }
//
//            if ((ptcWord == EC_NULL)
//                || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            OsSnprintf(szENIFilename, sizeof(szENIFilename) - 1, "%s", ptcWord); //将eni文件名保存到szENIFilename中
//        } else if (0 == OsStricmp(ptcWord, "-log")) {
//            EC_T_INT nPtcWordIndex = 4;
//
//            /* Search for the start of the config file name. The config file
//               name may start with quotation marks because of blanks in the filename */
//            while (ptcWord[nPtcWordIndex] != '\0') {
//                if ((ptcWord[nPtcWordIndex] == '\"') || (ptcWord[nPtcWordIndex] != ' ')) {
//                    break;
//                }
//                nPtcWordIndex++;
//            }
//
//            /* Depending of a config file name within quotation marks (or without
//               quotation marks) extract the filename */
//            if (ptcWord[nPtcWordIndex] == '\"') {
//                /* Check if the strtok position is already correct */
//                if (nPtcWordIndex > 3) {
//                    /* More than 1 blank before -f. Correct strtok position. */
//                    OsStrtok(EC_NULL, "\"");
//                }
//
//                /* Now extract the config file name */
//                ptcWord = OsStrtok(EC_NULL, "\"");
//            } else {
//                /* Extract the config file name if it was not set within quotation marks */
//                ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//            }
//
//            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            OsSnprintf(szLogFilePrefix, sizeof(szLogFilePrefix) - 1, "%s", ptcWord); // 将prefix保存到szLogFilePrefix中
//        } else if (OsStricmp(ptcWord, "-t") == 0) {
//            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            dwDuration = OsStrtol(ptcWord, EC_NULL, 0);
//        } else if (OsStricmp(ptcWord, "-auxclk") == 0) {
//#if (defined AUXCLOCK_SUPPORTED)
//            TimingDesc.bUseAuxClock = EC_TRUE;
//            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            TimingDesc.dwBusCycleTimeUsec = OsStrtol(ptcWord, EC_NULL, 0);
//            if (TimingDesc.dwBusCycleTimeUsec < 10) {
//                TimingDesc.dwBusCycleTimeUsec = 10;
//            }
//#else
//            EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Auxiliary clock not supported by this operating system!)\n"));
//            goto Exit;
//#endif
//        } else if (OsStricmp(ptcWord, "-b") == 0) {
//#if (defined AUXCLOCK_SUPPORTED)
//            if (TimingDesc.bUseAuxClock) {
//                EcLogMsg(EC_LOG_LEVEL_INFO,
//                         (pEcLogContext, EC_LOG_LEVEL_INFO, "Using bus cycle time %d usec from auxclock parameter\n", TimingDesc.dwBusCycleTimeUsec));
//            } else
//#endif
//            {
//                ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//                if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                    nRetVal = SYNTAX_ERROR;
//                    goto Exit;
//                }
//                TimingDesc.dwBusCycleTimeUsec = OsStrtol(ptcWord, EC_NULL, 0);
//            }
//        } else if (OsStricmp(ptcWord, "-a") == 0) {
//            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            dwCpuIndex = OsStrtol(ptcWord, EC_NULL, 0);
//        } else if (OsStricmp(ptcWord, "-v") == 0) {
//            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            nVerbose = OsStrtol(ptcWord, EC_NULL, 10);
//        } else if (OsStricmp(ptcWord, "-perf") == 0) {
//            bEnaPerfJobs = EC_TRUE;
//        } else if (OsStricmp(ptcWord, "-flash") == 0) {
//            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            nFlashAddress = OsStrtol(ptcWord, EC_NULL, 10);
//        }
//#if (defined ATEMRAS_SERVER)
//        else if (OsStricmp(ptcWord, "-sp") == 0) {
//            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//            if ((ptcWord == NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
//                wServerPort = ATEMRAS_DEFAULT_PORT;
//
//                /* optional sub parameter not found, use the current word for the next parameter */
//                bGetNextWord = EC_FALSE;
//            } else {
//                wServerPort = (EC_T_WORD) OsStrtol(ptcWord, EC_NULL, 10);
//            }
//        }
//#endif
//        else if (OsStricmp(ptcWord, "-vlan") == 0) {
//#if (defined VLAN_FRAME_SUPPORT)
//            bVLANEnable = EC_TRUE;
//            ptcWord = GetNextWord((EC_T_CHAR**)&szCommandLine, &tcStorage);
//            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0))
//            {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            wVLANId    = (EC_T_WORD)OsStrtol(ptcWord, EC_NULL, 0);
//            ptcWord = GetNextWord((EC_T_CHAR**)&szCommandLine, &tcStorage);
//            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0))
//            {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            byVLANPrio = (EC_T_BYTE)OsStrtol(ptcWord, EC_NULL, 0);
//#else
//            EcLogMsg(EC_LOG_LEVEL_INFO,
//                     (pEcLogContext, EC_LOG_LEVEL_INFO, "VLAN is not supported by this version!)\n"));
//            goto Exit;
//#endif
//        } else if (OsStricmp(ptcWord, "-dcmmode") == 0) {
//            /* Extract the config file name if it was not set within quotation marks */
//            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//
//            if (OsStricmp(ptcWord, "off") == 0) eDcmMode = eDcmMode_Off;
//            else if (OsStricmp(ptcWord, "busshift") == 0) eDcmMode = eDcmMode_BusShift;
//            else if (OsStricmp(ptcWord, "mastershift") == 0) eDcmMode = eDcmMode_MasterShift;
//            else if (OsStricmp(ptcWord, "masterrefclock") == 0) eDcmMode = eDcmMode_MasterRefClock;
//            else if (OsStricmp(ptcWord, "linklayerrefclock") == 0) eDcmMode = eDcmMode_LinkLayerRefClock;
//#if (defined INCLUDE_DCX)
//            else if (OsStricmp(ptcWord, "dcx") == 0) eDcmMode = eDcmMode_Dcx;
//#endif
//            else {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//        } else if (OsStricmp(ptcWord, "-ctloff") == 0) {
//            bCtlOff = EC_TRUE;
//        }
//            ///// ROS node insert
//        else if (strstr(ptcWord, "__")) { ;
//        } else {
//            EC_T_CHAR *szNextParm = ptcWord;
//            EC_T_DWORD dwNewCycleDurationUsec = 0;
//
//            dwRes = CreateLinkParmsFromCmdLine(&ptcWord, (EC_T_CHAR **) &szCommandLine, &tcStorage, &bGetNextWord,
//                                               &apLinkParms[dwNumLinkLayer]
//#if defined(INCLUDE_TTS)
//                    ,&dwNewCycleDurationUsec
//                    ,&pvTtsCycleEvent
//#endif
//            );
//            if (EC_E_NOERROR != dwRes) {
//                EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "SYNTAX_ERROR: %s!\n", szNextParm));
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            }
//            if (dwNumLinkLayer > 1) {
//                nRetVal = SYNTAX_ERROR;
//                goto Exit;
//            } else {
//                EC_CPUSET_ZERO(CpuSet);
//                EC_CPUSET_SET(CpuSet, dwCpuIndex);
//                apLinkParms[dwNumLinkLayer]->dwIstPriority = (CpuSet << 16) | RECV_THREAD_PRIO;
//
//                dwNumLinkLayer++;
//
//                if (dwNewCycleDurationUsec != 0) {
//                    TimingDesc.dwBusCycleTimeUsec = dwNewCycleDurationUsec;
//                }
//            }
//        }
//        /* get next word */
//        if (bGetNextWord) {
//            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
//        }
//        bGetNextWord = EC_TRUE;
//    } // End parse command line




    /* set application log level */
    switch (nVerbose) {
        case 0:
            G_pEcLogParms->dwLogLevel = EC_LOG_LEVEL_SILENT;
            break;
        case 1:
            G_pEcLogParms->dwLogLevel = EC_LOG_LEVEL_INFO;
            break;
        case 2:
            G_pEcLogParms->dwLogLevel = EC_LOG_LEVEL_INFO;
            break;
        case 3:
            G_pEcLogParms->dwLogLevel = EC_LOG_LEVEL_INFO;
            break;
        case 4:
            G_pEcLogParms->dwLogLevel = EC_LOG_LEVEL_INFO;
            break;
        case 5:
            G_pEcLogParms->dwLogLevel = EC_LOG_LEVEL_VERBOSE;
            break;
        default: /* no break */
        case 6:
            G_pEcLogParms->dwLogLevel = EC_LOG_LEVEL_VERBOSE_CYC;
            break;
    }
    /* initialize logging */
    oLogging.InitLogging(INSTANCE_MASTER_DEFAULT, G_pEcLogParms->dwLogLevel, LOG_ROLLOVER, LOG_THREAD_PRIO, dwCpuIndex,
                         szLogFilePrefix, LOG_THREAD_STACKSIZE);
    G_pEcLogParms->pfLogMsg = CAtEmLogging::LogMsg;
    G_pEcLogParms->pLogContext = (struct _EC_T_LOG_CONTEXT *) &oLogging;
    bLogInitialized = EC_TRUE;

#if !(defined XENOMAI) || (defined CONFIG_XENO_COBALT) || (defined CONFIG_XENO_MERCURY)
    oLogging.SetLogThreadAffinity(dwCpuIndex);
#endif /* !XENOMAI || CONFIG_XENO_COBALT || CONFIG_XENO_MERCURY */
//    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Full command line: %s\n", szFullCommandLine)); // 打印完整的命令行，已经不需要了 by think

    /* determine master configuration type */
    if ('\0' != szENIFilename[0]) {
        eCnfType = eCnfType_Filename;
        pbyEni = (EC_T_BYTE *) &szENIFilename[0];
        dwCnfDataLen = 256;
    } else {
#if (defined STATIC_MASTERENI_XML_DATA)
        eCnfType     = eCnfType_Data;
        pbyCnfData   = STATIC_MASTERENI_XML_DATA;
        dwCnfDataLen = STATIC_MASTERENI_XML_DATA_SIZE;
#else
        eCnfType = eCnfType_GenPreopENI;
#endif
    }

    if (0 == dwNumLinkLayer) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Syntax error: missing link layer command line parameter\n"));
        nRetVal = SYNTAX_ERROR;
        goto Exit;
    }

    /* for multi core cpus: select cpu number for this thread */
    EC_CPUSET_ZERO(CpuSet);
    EC_CPUSET_SET(CpuSet, dwCpuIndex);
    if (!OsSetThreadAffinity(EC_NULL, CpuSet)) {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Set Affinity Failed!\n"));
    }
    TimingDesc.dwCpuIndex = dwCpuIndex;

#if (defined EC_VERSION_UC3) && (defined NO_OS)
    TimingDesc.pvTimingEvent = pvTimingEvent;
#else
    /* create timing event to trigger the job task */
    TimingDesc.pvTimingEvent = OsCreateEvent();

#if defined(INCLUDE_TTS)
    TimingDesc.pvTtsEvent = pvTtsCycleEvent;
#endif

    if (EC_NULL == TimingDesc.pvTimingEvent) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: insufficient memory to create timing event!\n"));
        goto Exit;
    }
#endif

#if ((defined LINUX) && (defined AUXCLOCK_SUPPORTED))
    TimingDesc.bUseAuxClock = EC_TRUE;
#endif /* LINUX && AUXCLOCK_SUPPORTED */

#if (defined AUXCLOCK_SUPPORTED)
    /* initialize auxiliary clock */
    if (TimingDesc.bUseAuxClock) {
        dwRes = OsAuxClkInit(dwCpuIndex, 1000000 / TimingDesc.dwBusCycleTimeUsec, TimingDesc.pvTimingEvent);
        if (EC_E_NOERROR != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR at auxiliary clock initialization!\n"));
            goto Exit;
        }
    } /* TimingDesc.bUseAuxClock */
    else
#endif
    {
        bStartTimingTask = EC_TRUE;
    }

    /* create timing task if needed */
    //! Start tEcTimingTask
    if (bStartTimingTask) {
        OsCreateThread((EC_T_CHAR *) "tEcTimingTask", (EC_PF_THREADENTRY) tEcTimingTask, TIMER_THREAD_PRIO,
                       LOG_THREAD_STACKSIZE, (EC_T_VOID *) &TimingDesc);
        while (!TimingDesc.bIsRunning) {
            OsSleep(1);
        }
    }
    EcLogMsg(EC_LOG_LEVEL_INFO,
             (pEcLogContext, EC_LOG_LEVEL_INFO, "Run demo now with cycle time %d usec\n", TimingDesc.dwBusCycleTimeUsec));
#if (defined AUXCLOCK_SUPPORTED)
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Using %s\n",
            (TimingDesc.bUseAuxClock ? "AuxClock" : "Sleep")));
#endif

#ifdef INCLUDE_EMLL_STATIC_LIBRARY
    OsReplaceGetLinkLayerRegFunc(&DemoGetLinkLayerRegFunc);
#endif
    //! EtherCAT Master start processing data
    dwRes = ecatProcess(pbyCnf, eCnfType, pbyEni, dwCnfDataLen,
                        TimingDesc.dwBusCycleTimeUsec, nVerbose, dwDuration,
                        apLinkParms[0],
                        TimingDesc.pvTimingEvent,
#if defined(INCLUDE_TTS)
            TimingDesc.pvTtsEvent,
#endif
                        dwCpuIndex,
                        bEnaPerfJobs, nFlashAddress
#if (defined ATEMRAS_SERVER)
            , wServerPort
#endif
#if (defined VLAN_FRAME_SUPPORT)
            , bVLANEnable, wVLANId, byVLANPrio
#endif
            , ((2 == dwNumLinkLayer) ? apLinkParms[1] : EC_NULL), eDcmMode, bCtlOff, pOsParms
    );
    if (EC_E_NOERROR != dwRes) {
        goto Exit;
    }
    /* no errors */
    nRetVal = APP_NOERROR;

    Exit:
    if (nRetVal == SYNTAX_ERROR) {
        ShowSyntax();
    }
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "EcMasterDemoDc stop.\n"));

    if (nRetVal != APP_NOERROR) {
        OsSleep(5000);
    }

    /* stop timing task if running */
    if (TimingDesc.bIsRunning) {
        TimingDesc.bShutdown = EC_TRUE;
        while (TimingDesc.bIsRunning) {
            OsSleep(1);
        }
    }

#if (defined AUXCLOCK_SUPPORTED)
    /* clean up auxclk */
    if (TimingDesc.bUseAuxClock) {
        OsAuxClkDeinit(0);
    }
#endif
    /* delete the timing event */
    if (EC_NULL != TimingDesc.pvTimingEvent) {
        OsDeleteEvent(TimingDesc.pvTimingEvent);
        TimingDesc.pvTimingEvent = EC_NULL;
    }

    if (bLogInitialized) {
        /* de-initialize message logging */
        oLogging.DeinitLogging();
    }
    /* final OS layer cleanup */
    OsDeinit();

    /* free link parms created by CreateLinkParmsFromCmdLine() */
    for (; dwNumLinkLayer != 0; dwNumLinkLayer--) {
        if (EC_NULL != apLinkParms[dwNumLinkLayer - 1]) {
            OsFree(apLinkParms[dwNumLinkLayer - 1]);
            apLinkParms[dwNumLinkLayer - 1] = EC_NULL;
        }
    }

    return nRetVal;
}

/*-END OF SOURCE FILE--------------------------------------------------------*/
