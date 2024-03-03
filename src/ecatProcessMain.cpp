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



//! \brief  The application entry point.
//! \return  Value 0 is returned.
int main(int nArgc, char *ppArgv[]) {



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
//        OsSnprintf(szConfigFilename, sizeof(szConfigFilename) - 1, "%s",
//                   FLAGS_config.c_str()); // 将config文件名保存到szConfigFilename中

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
        dwCpuIndex = FLAGS_id; // 将cpuidx保存到dwCpuIndex中(现在根据主站ID走)

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

//        auto instance = std::strtol(FLAGS_instance.c_str(), nullptr, 0);

        dwRes = CreateLinkParms(FLAGS_link, FLAGS_instance, FLAGS_mode, &apLinkParms[dwNumLinkLayer]);
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
             (pEcLogContext, EC_LOG_LEVEL_INFO, "ROCOS-ECM now with cycle time %d usec\n", TimingDesc.dwBusCycleTimeUsec));
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


}

/*-END OF SOURCE FILE--------------------------------------------------------*/
