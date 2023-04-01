/*-----------------------------------------------------------------------------
 * DCDemoMain.cpp
 * Copyright                acontis technologies GmbH, Weingarten, Germany
 * Response                 Stefan Zintgraf
 * Description              EC-Master demo main entrypoint
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
#include <signal.h>


#include <stdio.h>
#include <stdlib.h>

#include <stdlib.h>
#include <sys/syscall.h>
#include <string.h>


/*-DEFINES-------------------------------------------------------------------*/
#define COMMAND_LINE_BUFFER_LENGTH 512

#define AUXCLOCK_SUPPORTED

/*-TYPEDEFS------------------------------------------------------------------*/
typedef struct _EC_T_TIMING_DESC {
    EC_T_VOID *pvTimingEvent;      /* event handle */
    EC_T_DWORD dwBusCycleTimeUsec; /* cycle time in usec */
    EC_T_BOOL bShutdown;          /* EC_TRUE if aux thread shall shut down */
    EC_T_BOOL bIsRunning;         /* EC_TRUE if the aux thread is running */
    EC_T_DWORD dwCpuIndex;         /* SMP systems: CPU index */

    EC_T_BOOL bUseAuxClock;       /* Either connect to IRQ or use sleep */
    EC_T_VOID *pvAuxClkEvent;      /* event handle */

} EC_T_TIMING_DESC;

/*-LOCAL FUNCTIONS-----------------------------------------------------------*/
/********************************************************************************/
/** \brief  signal handler.
*
* \return N/A
*/
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
    sscanf(SystemName.release, "%d.%d.%d", &nMaj, &nMin, &nSub);
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

/********************************************************************************/
/** Show syntax
*
* Return: N/A
*/
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
#if (defined VLAN_FRAME_SUPPORT)
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

    return;
}

/// \brief Set event according to periodical sleep or aux clock
/// Cyclically sets an event for thread synchronization purposes.
/// Either use OsSleep() or use the aux clock by means of:
/// - Enable AUX clock if selected.
/// - Wait for IRQ, acknowledge IRQ, SetEvent in loop until shutdown
/// - Disable AUX clock
/// Return: N/A
static EC_T_VOID tEcTimingTask(EC_T_VOID *pvThreadParamDesc) {
    EC_T_TIMING_DESC *pTimingDesc = (EC_T_TIMING_DESC *) pvThreadParamDesc;
    EC_T_CPUSET CpuSet;
#if (defined __INTIME__)
    RTHANDLE hTimingAlarm;
#endif

    EC_CPUSET_ZERO(CpuSet);
    EC_CPUSET_SET(CpuSet, pTimingDesc->dwCpuIndex);
    OsSetThreadAffinity(EC_NULL, CpuSet);

#if ((defined UNDER_CE) && (_WIN32_WCE >= 0x600))
    /* enable auxilary clock */
    if (pTimingDesc->bUseAuxClock)
    {
        DWORD dwAuxClkFreq = 1000000 / pTimingDesc->dwBusCycleTimeUsec;

        if (!KernelIoControl((DWORD)IOCTL_AUXCLK_ENABLE, &dwAuxClkFreq, sizeof(DWORD), NULL, 0, NULL))
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error calling KernelIoControl(IOCTL_AUXCLK_ENABLE) (0x%08X)!\n", GetLastError()));
            goto Exit;
        }
    }
#elif (defined __INTIME__)
    hTimingAlarm = CreateRtAlarm(KN_REPEATER, pTimingDesc->dwBusCycleTimeUsec);
#endif

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

    return;
}

/********************************************************************************/
/** \brief  Demo Application entry point.
*
* \return  Value 0 is returned.
*/

int main(int nArgc, char *ppArgv[])
{
    //!< gflags parsing parameters
    gflags::SetVersionString(ROCOS_ECM_VERSION);
    gflags::ParseCommandLineFlags(&nArgc, &ppArgv, false);

    printf("The configuration file(-config): %s\n", FLAGS_config.c_str());
    printf("The ethercat network information(eni) file(-eni): %s\n", FLAGS_eni.c_str());
    printf("The performance measure: %s\n", FLAGS_perf ? "true" : "false");

//    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "The configuration file(-config): %s\n", FLAGS_config.c_str()));
//    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "The ethercat network information(eni) file(-eni): %s\n", FLAGS_eni.c_str()));
//    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "The performance measure: %s\n", FLAGS_perf ? "true" : "false"));


    int nRetVal = APP_ERROR;
    EC_T_DWORD dwRes = EC_E_ERROR;
    EC_T_BOOL bLogInitialized = EC_FALSE;
    EC_T_CHAR szCommandLine[COMMAND_LINE_BUFFER_LENGTH];
    EC_T_CHAR szFullCommandLine[COMMAND_LINE_BUFFER_LENGTH];
    EC_T_BOOL bGetNextWord = EC_TRUE;
    EC_T_CHAR *ptcWord = EC_NULL;
    EC_T_CHAR tcStorage = '\0';

    EC_T_CHAR szLogFileprefix[256] = {'\0'};
    EC_T_CNF_TYPE eCnfType = eCnfType_Unknown;
    EC_T_PBYTE pbyCnfData = 0;
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

    /* OS specific initialization */


#if ((defined EC_VERSION_UC3) && (defined NO_OS))
    dwRes = OsInit(pOsParms);
    if (EC_E_NOERROR != dwRes)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot initialize OS!\n"));

        goto Exit;
    }
#endif

#if (defined LINUX)
    dwRes = EnableRealtimeEnvironment();
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

#if (defined __INTEGRITY)
    WaitForFileSystemInitialization();
#endif

#if (defined EC_VERSION_GO32)
    if (!io_Init())
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Fail to initialize Vortex86 I/O library\n"));
        nRetVal = APP_ERROR;
        goto Exit;
    }
    if (!irq_Init())
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Fail to initialize Vortex86 IRQ library\n"));
        io_Close();     /* Deinit IOs */
        nRetVal = APP_ERROR;
        goto Exit;
    }
#endif

#if !(defined UNDER_RTSS)
    /* Seed the random-number generator with current time so that
     * the numbers will be different every time we run.
     */
    srand((unsigned) OsQueryMsecCount());
#endif

#if (defined EC_VERSION_RTXC)
    hccInit();
    f_enterFS();   
    PMU_WritePerformanceCounterEvent(THIS_PMU_COUNTER_ID, 0x11u); // 0x11: CPU Cycles (250 MHz)
    PMU_WritePerformanceCounter(THIS_PMU_COUNTER_ID, 0x1); /* reset counter */
    PMU_EnablePerformanceCounter(THIS_PMU_COUNTER_ID);
#endif

    /* set running flag */
    bRun = EC_TRUE;

    /* Initialize Timing Event descriptor */
    TimingDesc.bShutdown = EC_FALSE;
    TimingDesc.bIsRunning = EC_FALSE;
    TimingDesc.dwBusCycleTimeUsec = CYCLE_TIME * 1000; //在1415行，如果-b参数，就TimingDesc.dwBusCycleTimeUsec设置为相应值

    /* prepare command line */
#if (defined VXWORKS) && (!defined __RTP__)
    OsStrncpy(szCommandLine, lpCmdLine, sizeof(szCommandLine) - 1);
#elif (defined RTOS_32)
#if (defined ECWIN_RTOS32)
    {
    VMF_HANDLE hEcatKey;
    VMF_CONFIG_ADDDATA AddData;
    UINT32 dwLength = 0;

        dwRes = vmfConfigRegKeyOpenA(VMF_CONFIGREG_HKEY_OS_CURRENT, "Ecat", &hEcatKey);
        if (RTE_SUCCESS == dwRes)
        {

            dwLength = sizeof(szCommandLine);
            vmfConfigRegValueQueryA(hEcatKey, "CommandLine", NULL, NULL, (UINT8*)&szCommandLine[0], &dwLength );
            vmfConfigRegKeyClose(hEcatKey);
        }
        if ('\0' == szCommandLine[0])
        {
            /* for compatibility */
            dwLength = sizeof(szCommandLine);
            dwRes = vmfConfigQueryValue( "Ecat", "CommandLine", VMF_CONFIG_SZ_TYPE, (UINT8*)&szCommandLine[0], &dwLength, &AddData);
            if (RTE_SUCCESS != dwRes)
            {
                EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot read EtherCAT demo command line, (EcatShm.config)\n"));
                EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Please, enter command line (e.g. atemDemo -v 2 -i8255x 1 1):\n"));
            }
        }
    }
#else
    OsStrncpy(szCommandLine, GetCommandLine(), sizeof(szCommandLine) - 1);
#endif /* !ECWIN_RTOS32 */
#elif (defined __TKERNEL) && (defined __arm__)
    /*-fixed command line for T-Kernel on ARM */
    OsStrncpy(szCommandLine, "-l9218i 1 -v 1 -t 0 -f eni.xml", sizeof(szCommandLine) - 1);
#elif (defined EC_VERSION_SYSBIOS) || (defined EC_VERSION_RIN32M3) || (defined EC_VERSION_XILINX_STANDALONE) || \
    (defined EC_VERSION_ETKERNEL) || (defined EC_VERSION_RZT1) || (defined EC_VERSION_RZGNOOS) || (defined EC_VERSION_JSLWARE) || \
    (defined EC_VERSION_UCOS) || (defined EC_VERSION_RTXC)
    OsStrncpy(szCommandLine, DEMO_PARAMETERS, sizeof(szCommandLine) - 1);
#elif (defined EC_VERSION_UC3)
    OsStrncpy(szCommandLine, pszCommandLine, sizeof(szCommandLine) - 1);
#elif (defined EC_VERSION_FREERTOS)
    OsStrncpy(szCommandLine, pszCommandLine, sizeof(szCommandLine) - 1);
#elif (defined EC_VERSION_RTEMS)
    /* copy cmdline without the applications name(first token) */
    OsStrncpy(szCommandLine, strchr(bsp_boot_cmdline,' '),sizeof(szCommandLine) - 1);
#elif (defined EC_VERSION_ECOS) && (defined __arm__)
    /*copy cmdline */
    OsStrncpy(szCommandLine, pCmdLine, sizeof(szCommandLine));
#else
    /* build szCommandLine from argument list */
    {
        EC_T_CHAR *pcStrCur = szCommandLine;
        EC_T_INT nStrRemain = COMMAND_LINE_BUFFER_LENGTH;
#if (defined UNDER_CE)
        EC_T_CHAR  szStrFormat[] = "%S"; /* convert UNICODE to multibyte */
#else
        EC_T_CHAR szStrFormat[] = "%s";
#endif
        /* build szCommandLine from argument list, skipping executable name */
        for (nArgc--, ppArgv++; nArgc > 0; nArgc--, ppArgv++) {
            EC_T_BOOL bIsFileName = EC_FALSE;

            /* insert next argument */
            OsSnprintf(pcStrCur, nStrRemain - 1, szStrFormat, *ppArgv);

            /* check for file name */
            if (0 == OsStrcmp(pcStrCur, "-f")) {
                bIsFileName = EC_TRUE;
            }
            /* adjust string cursor */
            nStrRemain -= (EC_T_INT) OsStrlen(pcStrCur);
            pcStrCur = pcStrCur + OsStrlen(pcStrCur);

            /* insert space */
            OsStrncpy(pcStrCur, " ", nStrRemain - 1);
            nStrRemain--;
            pcStrCur++;

            if (bIsFileName && (1 < nArgc)) {
                /* move to next arg (ENI file name) */
                nArgc--;
                ppArgv++;

                /* insert quotation mark */
                OsStrncpy(pcStrCur, "\"", nStrRemain - 1);
                nStrRemain--;
                pcStrCur++;

                /* insert ENI file name */
                OsSnprintf(pcStrCur, nStrRemain - 1, szStrFormat, *ppArgv);
                nStrRemain -= (EC_T_INT) OsStrlen(pcStrCur);
                pcStrCur = pcStrCur + OsStrlen(pcStrCur);

                /* insert quotation mark */
                OsStrncpy(pcStrCur, "\" ", nStrRemain - 1);
                nStrRemain--;
                pcStrCur++;
            }
        }
    }
#endif
    /* backup full command line */
    OsStrncpy(szFullCommandLine, szCommandLine, sizeof(szFullCommandLine) - 1);

    /* parse command line */
    for (ptcWord = OsStrtok(szCommandLine, " "); ptcWord != EC_NULL;) {
        if (0 == OsStricmp(ptcWord, "-f")) {
            EC_T_INT nPtcWordIndex = 3;

            /* Search for the start of the config file name. The config file
               name may start with quotation marks because of blanks in the filename */
            while (ptcWord[nPtcWordIndex] != '\0') {
                if ((ptcWord[nPtcWordIndex] == '\"') || (ptcWord[nPtcWordIndex] != ' ')) {
                    break;
                }
                nPtcWordIndex++;
            }

            /* Depending of a config file name within quotation marks (or without
               quotation marks) extract the filename */
            if (ptcWord[nPtcWordIndex] == '\"') {
                /* Check if the strtok position is already correct */
                if (nPtcWordIndex > 3) {
                    /* More than 1 blank before -f. Correct strtok position. */
                    OsStrtok(EC_NULL, "\"");
                }

                /* Now extract the config file name */
                ptcWord = OsStrtok(EC_NULL, "\"");
            } else {
                /* Extract the config file name if it was not set within quotation marks */
                ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
            }

            if ((ptcWord == EC_NULL)
                || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            OsSnprintf(szENIFilename, sizeof(szENIFilename) - 1, "%s", ptcWord);
        } else if (0 == OsStricmp(ptcWord, "-log")) {
            EC_T_INT nPtcWordIndex = 4;

            /* Search for the start of the config file name. The config file
               name may start with quotation marks because of blanks in the filename */
            while (ptcWord[nPtcWordIndex] != '\0') {
                if ((ptcWord[nPtcWordIndex] == '\"') || (ptcWord[nPtcWordIndex] != ' ')) {
                    break;
                }
                nPtcWordIndex++;
            }

            /* Depending of a config file name within quotation marks (or without
               quotation marks) extract the filename */
            if (ptcWord[nPtcWordIndex] == '\"') {
                /* Check if the strtok position is already correct */
                if (nPtcWordIndex > 3) {
                    /* More than 1 blank before -f. Correct strtok position. */
                    OsStrtok(EC_NULL, "\"");
                }

                /* Now extract the config file name */
                ptcWord = OsStrtok(EC_NULL, "\"");
            } else {
                /* Extract the config file name if it was not set within quotation marks */
                ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
            }

            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            OsSnprintf(szLogFileprefix, sizeof(szLogFileprefix) - 1, "%s", ptcWord);
        } else if (OsStricmp(ptcWord, "-t") == 0) {
            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            dwDuration = OsStrtol(ptcWord, EC_NULL, 0);
        } else if (OsStricmp(ptcWord, "-auxclk") == 0) {
#if (defined AUXCLOCK_SUPPORTED)
            TimingDesc.bUseAuxClock = EC_TRUE;
            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            TimingDesc.dwBusCycleTimeUsec = OsStrtol(ptcWord, EC_NULL, 0);
            if (TimingDesc.dwBusCycleTimeUsec < 10) {
                TimingDesc.dwBusCycleTimeUsec = 10;
            }
#else
            EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Auxiliary clock not supported by this operating system!)\n"));
            goto Exit;
#endif
        } else if (OsStricmp(ptcWord, "-b") == 0) {
#if (defined AUXCLOCK_SUPPORTED)
            if (TimingDesc.bUseAuxClock) {
                EcLogMsg(EC_LOG_LEVEL_INFO,
                         (pEcLogContext, EC_LOG_LEVEL_INFO, "Using bus cycle time %d usec from auxclock parameter\n", TimingDesc.dwBusCycleTimeUsec));
            } else
#endif
            {
                ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
                if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                    nRetVal = SYNTAX_ERROR;
                    goto Exit;
                }
                TimingDesc.dwBusCycleTimeUsec = OsStrtol(ptcWord, EC_NULL, 0);
            }
        } else if (OsStricmp(ptcWord, "-a") == 0) {
            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            dwCpuIndex = OsStrtol(ptcWord, EC_NULL, 0);
        } else if (OsStricmp(ptcWord, "-v") == 0) {
            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            nVerbose = OsStrtol(ptcWord, EC_NULL, 10);
        } else if (OsStricmp(ptcWord, "-perf") == 0) {
            bEnaPerfJobs = EC_TRUE;
        } else if (OsStricmp(ptcWord, "-flash") == 0) {
            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            nFlashAddress = OsStrtol(ptcWord, EC_NULL, 10);
        }
#if (defined ATEMRAS_SERVER)
        else if (OsStricmp(ptcWord, "-sp") == 0) {
            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
            if ((ptcWord == NULL) || (OsStrncmp(ptcWord, "-", 1) == 0)) {
                wServerPort = ATEMRAS_DEFAULT_PORT;

                /* optional sub parameter not found, use the current word for the next parameter */
                bGetNextWord = EC_FALSE;
            } else {
                wServerPort = (EC_T_WORD) OsStrtol(ptcWord, EC_NULL, 10);
            }
        }
#endif
        else if (OsStricmp(ptcWord, "-vlan") == 0) {
#if (defined VLAN_FRAME_SUPPORT)
            bVLANEnable = EC_TRUE;
            ptcWord = GetNextWord((EC_T_CHAR**)&szCommandLine, &tcStorage);
            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0))
            {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            wVLANId    = (EC_T_WORD)OsStrtol(ptcWord, EC_NULL, 0);
            ptcWord = GetNextWord((EC_T_CHAR**)&szCommandLine, &tcStorage);
            if ((ptcWord == EC_NULL) || (OsStrncmp(ptcWord, "-", 1) == 0))
            {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            byVLANPrio = (EC_T_BYTE)OsStrtol(ptcWord, EC_NULL, 0);
#else
            EcLogMsg(EC_LOG_LEVEL_INFO,
                     (pEcLogContext, EC_LOG_LEVEL_INFO, "VLAN is not supported by this version!)\n"));
            goto Exit;
#endif
        } else if (OsStricmp(ptcWord, "-dcmmode") == 0) {
            /* Extract the config file name if it was not set within quotation marks */
            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);

            if (OsStricmp(ptcWord, "off") == 0) eDcmMode = eDcmMode_Off;
            else if (OsStricmp(ptcWord, "busshift") == 0) eDcmMode = eDcmMode_BusShift;
            else if (OsStricmp(ptcWord, "mastershift") == 0) eDcmMode = eDcmMode_MasterShift;
            else if (OsStricmp(ptcWord, "masterrefclock") == 0) eDcmMode = eDcmMode_MasterRefClock;
            else if (OsStricmp(ptcWord, "linklayerrefclock") == 0) eDcmMode = eDcmMode_LinkLayerRefClock;
#if (defined INCLUDE_DCX)
            else if (OsStricmp(ptcWord, "dcx") == 0) eDcmMode = eDcmMode_Dcx;
#endif
            else {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
        } else if (OsStricmp(ptcWord, "-ctloff") == 0) {
            bCtlOff = EC_TRUE;
        }
            ///// ROS node insert
        else if (strstr(ptcWord, "__")) { ;
        } else {
            EC_T_CHAR *szNextParm = ptcWord;
            EC_T_DWORD dwNewCycleDurationUsec = 0;

            dwRes = CreateLinkParmsFromCmdLine(&ptcWord, (EC_T_CHAR **) &szCommandLine, &tcStorage, &bGetNextWord,
                                               &apLinkParms[dwNumLinkLayer]
#if defined(INCLUDE_TTS)
                    ,&dwNewCycleDurationUsec
                    ,&pvTtsCycleEvent
#endif
            );
            if (EC_E_NOERROR != dwRes) {
                EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "SYNTAX_ERROR: %s!\n", szNextParm));
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            }
            if (dwNumLinkLayer > 1) {
                nRetVal = SYNTAX_ERROR;
                goto Exit;
            } else {
#ifdef LINUX
                EC_CPUSET_ZERO(CpuSet);
                EC_CPUSET_SET(CpuSet, dwCpuIndex);
                apLinkParms[dwNumLinkLayer]->dwIstPriority = (CpuSet << 16) | RECV_THREAD_PRIO;
#else
                apLinkParms[dwNumLinkLayer]->dwIstPriority = RECV_THREAD_PRIO;
#endif
                dwNumLinkLayer++;

                if (dwNewCycleDurationUsec != 0) {
                    TimingDesc.dwBusCycleTimeUsec = dwNewCycleDurationUsec;
                }
            }
        }
        /* get next word */
        if (bGetNextWord) {
            ptcWord = GetNextWord((EC_T_CHAR **) &szCommandLine, &tcStorage);
        }
        bGetNextWord = EC_TRUE;
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
                         szLogFileprefix, LOG_THREAD_STACKSIZE);
    G_pEcLogParms->pfLogMsg = CAtEmLogging::LogMsg;
    G_pEcLogParms->pLogContext = (struct _EC_T_LOG_CONTEXT *) &oLogging;
    bLogInitialized = EC_TRUE;

#if !(defined XENOMAI) || (defined CONFIG_XENO_COBALT) || (defined CONFIG_XENO_MERCURY)
    oLogging.SetLogThreadAffinity(dwCpuIndex);
#endif /* !XENOMAI || CONFIG_XENO_COBALT || CONFIG_XENO_MERCURY */
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Full command line: %s\n", szFullCommandLine));

    /* determine master configuration type */
    if ('\0' != szENIFilename[0]) {
        eCnfType = eCnfType_Filename;
        pbyCnfData = (EC_T_BYTE *) &szENIFilename[0];
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
#if (defined VXWORKS)
        sysAuxClkDisable();
        if (OK != sysAuxClkRateSet(1000000 / TimingDesc.dwBusCycleTimeUsec))
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error calling sysAuxClkRateSet!\n"));
            goto Exit;
        }
#if ( (_WRS_VXWORKS_MAJOR == 6) && (_WRS_VXWORKS_MINOR >= 9) && (_WRS_CONFIG_LP64) ) || (_WRS_VXWORKS_MAJOR > 6)
        sysAuxClkConnect((FUNCPTR)vxAuxClkIsr, (_Vx_usr_arg_t)TimingDesc.pvTimingEvent);
#else
        sysAuxClkConnect((FUNCPTR)vxAuxClkIsr, (EC_T_INT)TimingDesc.pvTimingEvent);
#endif
        sysAuxClkEnable( );
        OsSleep(2000);

#elif ((defined UNDER_CE) && (_WIN32_WCE >= 0x600))
        /* get auxilary clock sysintr */
        bRes = KernelIoControl((DWORD)IOCTL_AUXCLK_GET_SYSINTR, (DWORD)NULL, (DWORD)0, &dwAuxClkSysIntr, (DWORD)sizeof(DWORD), &dwWinRes);
        if (!bRes)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error calling KernelIoControl(IOCTL_AUXCLK_GET_SYSINTR) (0x%08X)!\n", GetLastError()));
            goto Exit;
        }
        /* open VirtualDrv for interrupt management */
        TimingDesc.hVirtualDrv = CreateFile(TEXT("VIR1:"),
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                 INVALID_HANDLE_VALUE);
        if ((TimingDesc.hVirtualDrv == NULL) || (TimingDesc.hVirtualDrv == INVALID_HANDLE_VALUE))
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error calling CreateFile(""VIR1:"") (0x%08X)!\n", GetLastError()));
            TimingDesc.hVirtualDrv = NULL;
            goto Exit;
        }
        /* connect auxilary clock interrupt */
        TimingDesc.oIrqDesc.dwSysIrq = dwAuxClkSysIntr;
        swprintf(TimingDesc.oIrqDesc.szEventName, TEXT("%s"), TEXT("AUXCLK"));
        bRes = DeviceIoControl(TimingDesc.hVirtualDrv, (DWORD)IOCTL_VIRTDRV_INTERRUPT_INIT, &(TimingDesc.oIrqDesc), sizeof(VI_T_INTERRUPTDESC), NULL, 0, NULL, NULL );
        if (!bRes)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error calling DeviceIoControl(IOCTL_VIRTDRV_INTERRUPT_INIT) (0x%08X)!\n", GetLastError()));
            goto Exit;
        }
        /* create auxilary clock interrupt event */
        TimingDesc.pvAuxClkEvent = (VOID*)CreateEvent(NULL, FALSE, FALSE, TEXT("AUXCLK"));
        if ((TimingDesc.pvAuxClkEvent == NULL) || (TimingDesc.pvTimingEvent == INVALID_HANDLE_VALUE))
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error creating AuxClk event (0x%08X)!\n", GetLastError()));
            TimingDesc.pvAuxClkEvent = NULL;
            goto Exit;
        }
        /* auxiliary clock event handled within timing task */
        bStartTimingTask = EC_TRUE;

#elif (defined UNDER_RTSS)
        hTimer = RtCreateTimer(NULL, 0, RtxAuxClkIsr, (PVOID)TimingDesc.pvTimingEvent, RT_PRIORITY_MAX, CLOCK_2);
        liTimer.QuadPart = (LONGLONG)10*TimingDesc.dwBusCycleTimeUsec;
        RtSetTimerRelative(hTimer, &liTimer, &liTimer);
#else /* !UNDER_RTSS */
        dwRes = OsAuxClkInit(dwCpuIndex, 1000000 / TimingDesc.dwBusCycleTimeUsec, TimingDesc.pvTimingEvent);
        if (EC_E_NOERROR != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR at auxiliary clock initialization!\n"));
            goto Exit;
        }
#endif /* !UNDER_RTSS */
    } /* TimingDesc.bUseAuxClock */
    else
#endif
    {
#if (defined NO_OS)
        bStartTimingTask = EC_FALSE;
#elif (defined RTOS_32)
        CLKSetTimerIntVal(TimingDesc.dwBusCycleTimeUsec);
        RTKDelay(1);
        bStartTimingTask = EC_TRUE;
#elif (defined EC_VERSION_RTEMS)
        status = rtems_timer_create(rtems_build_name('E', 'C', 'T', 'T'), &timerId);
        if(RTEMS_SUCCESSFUL != status)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ATEMDemoMain: cannot create timer\nRTEMS returned: %s\n",
                rtems_status_text(status)));
            goto Exit;
        }
#if (defined RTEMS_USE_TIMER_SERVER)
        status = rtems_timer_initiate_server(TIMER_THREAD_PRIO,TIMER_THREAD_STACKSIZE,0);
        if(RTEMS_SUCCESSFUL != status)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ATEMDemoMain: cannot initialise timer\nRTEMS returned: %s\n",
                rtems_status_text(status)));
            goto Exit;
        }
        status = rtems_timer_server_fire_after(timerId,
        RTEMS_MICROSECONDS_TO_TICKS(TimingDesc.dwBusCycleTimeUsec),
        rtemsTimerIsr, TimingDesc.pvTimingEvent);
#else
        status = rtems_timer_fire_after(timerId,
                RTEMS_MICROSECONDS_TO_TICKS(TimingDesc.dwBusCycleTimeUsec),
                rtemsTimerIsr, TimingDesc.pvTimingEvent);
#endif /* RTEMS_USE_TIMER_SERVER */
        if(RTEMS_SUCCESSFUL != status)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ATEMDemoMain: cannot initialise timer\nRTEMS returned: %s\n",
                rtems_status_text(status)));
            goto Exit;
        }
        bStartTimingTask = EC_FALSE; //No timing task needed
#elif (defined EC_VERSION_QNX)
        EC_T_INT nRes = 0;
        EC_T_INT nTimerId = 0;
        _clockperiod newClockPeriod;
        sigevent timerSigEvent;
        itimerspec itime;

        /* calculate and set clock period */
#if (EC_VERSION_QNX >= 700)
        nRes = procmgr_ability(0, PROCMGR_ADN_ROOT|PROCMGR_AOP_ALLOW|PROCMGR_AID_CLOCKPERIOD, PROCMGR_AID_EOL);
        if (nRes != 0)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: procmgr_ability PROCMGR_AID_CLOCKPERIOD failed\n"));
            goto Exit;
        }
#endif
        OsMemset(&newClockPeriod, 0, sizeof(newClockPeriod));
        newClockPeriod.nsec = EC_AT_MOST((TimingDesc.dwBusCycleTimeUsec * 1000) / 2, 500000);
        nRes = ClockPeriod(CLOCK_REALTIME, &newClockPeriod, EC_NULL, 0);
        if (nRes != 0)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Set clock period failed\n"));
            goto Exit;
        }
        /* create and attach communication channel */
        TimingDesc.nTimerChannel = ChannelCreate(0);
        if (TimingDesc.nTimerChannel == -1)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Create timer channel failed\n"));
            goto Exit;
        }
        OsMemset(&timerSigEvent, 0, sizeof(timerSigEvent));
        timerSigEvent.sigev_notify = SIGEV_PULSE;
        timerSigEvent.sigev_coid = ConnectAttach(ND_LOCAL_NODE, 0, TimingDesc.nTimerChannel, _NTO_SIDE_CHANNEL, 0);
        if (timerSigEvent.sigev_coid == -1)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Attach timer channel failed\n"));
            goto Exit;
        }
        /* create timer */
#if (EC_VERSION_QNX >= 700)
        nRes = procmgr_ability(0, PROCMGR_ADN_ROOT|PROCMGR_AOP_ALLOW|PROCMGR_AID_TIMER, PROCMGR_AID_EOL);
        if (nRes != 0)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: procmgr_ability PROCMGR_AID_TIMER failed\n"));
            goto Exit;
        }
#endif
        timerSigEvent.sigev_priority = TIMER_THREAD_PRIO;
        timerSigEvent.sigev_code = _PULSE_CODE_MINAVAIL;
        nRes = timer_create(CLOCK_REALTIME, &timerSigEvent, &nTimerId);
        if (nRes != 0)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Create timer failed\n"));
            goto Exit;
        }
#if (EC_VERSION_QNX >= 700)
        /* use high resolution timer by set timer tolerance */
        OsMemset(&itime, 0, sizeof(itime));
        itime.it_value.tv_nsec = 1;
        nRes = timer_settime(nTimerId, TIMER_TOLERANCE, &itime, EC_NULL);
        if (nRes != 0)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Set timer tolerance failed\n"));
            goto Exit;
        }
#endif
        /* start timer */
        OsMemset(&itime, 0, sizeof(itime));
        itime.it_value.tv_nsec = TimingDesc.dwBusCycleTimeUsec * 1000;
        itime.it_interval.tv_nsec = TimingDesc.dwBusCycleTimeUsec * 1000;
        nRes = timer_settime(nTimerId, 0, &itime, EC_NULL);
        if (nRes != 0)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Start timer failed\n"));
            goto Exit;
        }
        bStartTimingTask = EC_TRUE;
#else
        bStartTimingTask = EC_TRUE;
#endif
    }

    /* create timing task if needed */
    //启动tEcTimingTask
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
#ifdef EC_VERSION_ETKERNEL
    if (bEnaPerfJobs)
    {
        InitPreventCpuIdleTask(); //Start
    }
#endif
#ifdef INCLUDE_EMLL_STATIC_LIBRARY
    OsReplaceGetLinkLayerRegFunc(&DemoGetLinkLayerRegFunc);
#endif
    //主站相关操作开始执行
    dwRes = ecatProcess(eCnfType, pbyCnfData, dwCnfDataLen,
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
#if !(defined VXWORKS)
    if (nRetVal != APP_NOERROR) {
        OsSleep(5000);
    }
#endif
    /* stop timing task if running */
#if (defined EC_VERSION_RTEMS)
    rtems_timer_delete(timerId);
#else
    if (TimingDesc.bIsRunning) {
        TimingDesc.bShutdown = EC_TRUE;
        while (TimingDesc.bIsRunning) {
            OsSleep(1);
        }
    }
#endif
#ifdef EC_VERSION_ETKERNEL
    if (bEnaPerfJobs  && S_bPreventCpuIdleTaskRunning)
    {
        S_bPreventCpuIdleTaskShutdown = EC_TRUE;
        while (S_bPreventCpuIdleTaskRunning)
        {
            OsSleep(1);
        }
    }
#endif
#if (defined AUXCLOCK_SUPPORTED)
    /* clean up auxclk */
    if (TimingDesc.bUseAuxClock) {
#if (defined VXWORKS)
        sysAuxClkDisable();

#elif ((defined UNDER_CE) && (_WIN32_WCE >= 0x600))
        if (NULL != TimingDesc.hVirtualDrv)
        {
            /* deinit the auxilary clock interrupt and close the handle to it */
            TimingDesc.oIrqDesc.dwSysIrq = dwAuxClkSysIntr;
            bRes = DeviceIoControl(TimingDesc.hVirtualDrv, (DWORD)IOCTL_VIRTDRV_INTERRUPT_DEINIT, &(TimingDesc.oIrqDesc), sizeof(VI_T_INTERRUPTDESC), NULL, 0, NULL, NULL );
            if (!bRes)
            {
                printf("Error calling DeviceIoControl(IOCTL_VIRTDRV_INTERRUPT_DEINIT) (0x%08X)!\n", GetLastError());
            }
            CloseHandle(TimingDesc.hVirtualDrv);
            TimingDesc.hVirtualDrv = NULL;
        }
        /* Close the AUXCLK-TimingTask synchronization handle */
        if (EC_NULL != TimingDesc.pvAuxClkEvent)
        {
            CloseHandle(TimingDesc.pvAuxClkEvent);
            TimingDesc.pvAuxClkEvent = EC_NULL;
        }
#elif (defined UNDER_RTSS)
        if (NULL != hTimer)
        {
             RtCancelTimer(hTimer, &liTimer);
             RtDeleteTimer(hTimer);
        }
#else
        OsAuxClkDeinit(0);
#endif
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

#if (defined EC_VERSION_GO32)
    irq_Close();    /* close the Vortex86 IRQ library */
    io_Close();     /* close the Vortex86 I/O library */
#endif

#if (defined EC_VERSION_RTXC)
    f_releaseFS();
#endif

#if (defined EC_VERSION_RTEMS)
    rtemsSyncBDBuffers();
    exit(nRetVal);
#else
    return nRetVal;
#endif
}

#if (defined EC_VERSION_RTEMS)
rtems_task Init(rtems_task_argument arg)
{
    rtems_id   Task_id;
    rtems_name Task_name = rtems_build_name('M','A','I','N');

    /* read time of day from rtc device and set it to rtems */
    setRealTimeToRTEMS();
    /* Mount file systems */
    rtemsMountFilesystems();
    /* create and start main task */
    rtems_task_create(Task_name, MAIN_THREAD_PRIO,
            RTEMS_MINIMUM_STACK_SIZE * 4, RTEMS_DEFAULT_MODES,
            RTEMS_FLOATING_POINT | RTEMS_DEFAULT_ATTRIBUTES, &Task_id);
    rtems_task_start(Task_id, Main, 1);
//  rtems_monitor_init(0);
    rtems_task_delete( RTEMS_SELF );
}
#endif /* EC_VERSION_RTEMS */

/*-END OF SOURCE FILE--------------------------------------------------------*/
