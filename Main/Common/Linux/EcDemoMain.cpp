/*-----------------------------------------------------------------------------
 * EcDemoMain.cpp
 * Copyright                acontis technologies GmbH, Ravensburg, Germany
 * Response                 Holger Oelhaf
 * Description              EtherCAT demo main entrypoint
 * Modified                 Yang Luo , luoyang@sia.cn
 * Last Modification Date   2023.04.04 18:00
 *----------------------------------------------------------------------------*/

/*-LOGGING-------------------------------------------------------------------*/
#ifndef pEcLogParms
#define pEcLogParms G_pEcLogParms
#endif

/*-INCLUDES------------------------------------------------------------------*/
#include "EcDemoApp.h"
#include "EcDemoTimingTaskPlatform.h"

#include <sys/mman.h>
#include <sys/utsname.h>
#include <csignal>

#include <cstdio>
#include <cstdlib>


//Add by think 2024.03.02
#include "EcFlags.h"
#include "ver.h"
#include <cstring>
#include <iostream>
#include <termcolor/termcolor.hpp>

/*-DEFINES-------------------------------------------------------------------*/

/*-TYPEDEFS------------------------------------------------------------------*/

/*-LOCAL FUNCTIONS-----------------------------------------------------------*/
EC_T_DWORD PrepareFlags(int nArgc, char* ppArgv[]) // Add by think 2024.03.02
{
    /// gflags parsing parameters
    gflags::SetVersionString(ROCOS_ECM_VERSION);
    gflags::ParseCommandLineFlags(&nArgc, &ppArgv, true);

    /// Print the configuration information
    std::cout << termcolor::on_bright_blue << termcolor::blink
              << "=================ROCOS EtherCAT Master=================" << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "==                  Author: think                    ==" << termcolor::reset
              << std::endl;
    std::cout << termcolor::blue << "==                  Company: Landau Robotics         ==" << termcolor::reset
              << std::endl;
    std::cout << termcolor::blue << "==                  Version: " << ROCOS_ECM_VERSION << "                   =="
              << termcolor::reset << std::endl;

    std::cout << termcolor::blue << "== Ec-Master ID(-id): " << termcolor::reset
              << termcolor::bold << FLAGS_id << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== EtherCAT network information file(-eni): " << termcolor::reset
              << termcolor::bold << FLAGS_eni << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Performance measure: " << termcolor::reset << termcolor::bold
              << FLAGS_perf << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Running duration(ms): " << termcolor::reset << termcolor::bold
              << (FLAGS_duration ? std::to_string(FLAGS_duration) : "forever") << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Bus cycle time(us): " << termcolor::reset << termcolor::bold << FLAGS_cycle
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Verbose level: " << termcolor::reset << termcolor::bold << FLAGS_verbose
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== CPU index: " << termcolor::reset << termcolor::bold << FLAGS_cpuidx
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== Remote API server port: " << termcolor::reset << termcolor::bold << FLAGS_sp
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== link layer: " << termcolor::reset << termcolor::bold << FLAGS_link
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== instance: " << termcolor::reset << termcolor::bold << FLAGS_instance
              << termcolor::reset << std::endl;
    std::cout << termcolor::blue << "== mode: " << termcolor::reset << termcolor::bold << FLAGS_mode << termcolor::reset
              << std::endl;

    std::cout << termcolor::on_bright_blue << termcolor::blink
              << "=======================================================" << termcolor::reset << std::endl;

    return EC_E_NOERROR;
}


EC_T_DWORD SetAppParmsFromFlags(T_EC_DEMO_APP_CONTEXT* pAppContext, T_EC_DEMO_APP_PARMS* pAppParms) // Add by think 2024.03.02
{
    EC_T_DWORD dwRetVal = EC_E_ERROR;
    EC_T_INT   nVerbose = 1;
    EC_T_BOOL  bGetNextWord = EC_TRUE;
    EC_T_CHAR* ptcWord = EC_NULL;
    EC_T_INT   nPtcWordIdx = 0;
#if (!defined ECMASTERRAS_CLIENT)
    EC_T_CHAR  tcStorage = '\0';
    EC_T_CHAR  szCfgFilename[256] = { '\0' };
#endif
    EC_T_CHAR  szCommandLineTmp[COMMAND_LINE_BUFFER_LENGTH];
    EC_T_BOOL  bNextInstanceDetected = EC_FALSE;

    gflags::CommandLineFlagInfo info;

    // -eni
    OsSnprintf(pAppParms->szENIFilename, sizeof(pAppParms->szENIFilename) - 1, "%s", FLAGS_eni.c_str()); //将eni文件名保存到szCfgFilename中
    pAppParms->eCnfType = eCnfType_Filename;
    pAppParms->pbyCnfData = (EC_T_BYTE*)&pAppParms->szENIFilename;
    pAppParms->dwCnfDataLen = (EC_T_DWORD)OsStrlen(pAppParms->szENIFilename);

    // -duration
    pAppParms->dwDemoDuration = FLAGS_duration; // -t 将duration保存到dwDuration中

    // -cycle
    pAppParms->dwBusCycleTimeUsec = FLAGS_cycle; // -b 1000  Add by think 2024.03.02


    // -verbose
    nVerbose = FLAGS_verbose; // -v 将verbose保存到nVerbose中
    pAppParms->nVerbose = nVerbose;
    SetLogLevels(pAppParms);

    // -id
    pAppParms->dwMasterInstanceId = FLAGS_id;
    pAppParms->dwCpuIndex = FLAGS_id; // 将cpuidx保存到dwCpuIndex中(现在根据主站ID走)
    EC_CPUSET_SET(pAppParms->CpuSet, pAppParms->dwCpuIndex);

    // -perf
    pAppParms->dwPerfMeasLevel = FLAGS_perf; // 将perf保存到bEnaPerfJobs中
    pAppParms->bPerfMeasShowCyclic = EC_TRUE;

    // -sp
#if (defined INCLUDE_RAS_SERVER)
    if(GetCommandLineFlagInfo("sp" ,&info) && !info.is_default) {
        pAppParms->bStartRasServer = EC_TRUE;
        pAppParms->wRasServerPort = FLAGS_sp; // 将sp保存到wServerPort中
    }
#endif

    // -log
    OsSnprintf(pAppParms->szLogFileprefix, sizeof(pAppParms->szLogFileprefix) - 1, "%s", FLAGS_log.c_str()); // 将prefix保存到szLogFilePrefix中

    // -flash
    if(GetCommandLineFlagInfo("flash" ,&info) && !info.is_default) {
        pAppParms->bFlash = EC_TRUE;
        pAppParms->wFlashSlaveAddr = FLAGS_flash; // 将flash保存到nFlashAddress中
    }

    // -dcmmode
    switch (FLAGS_dcmmode) {
        case 0:
            pAppParms->eDcmMode = eDcmMode_Off;
            break;
        case 1:
            pAppParms->eDcmMode = eDcmMode_BusShift;
            break;
        case 2:
            pAppParms->eDcmMode = eDcmMode_MasterShift;
            break;
        case 3:
            pAppParms->eDcmMode = eDcmMode_LinkLayerRefClock;
            break;
        case 4:
            pAppParms->eDcmMode = eDcmMode_MasterRefClock;
            break;
#if (defined INCLUDE_DCX)
        case 5:
            pAppParms->eDcmMode = eDcmMode_Dcx;
            break;
#endif
        default:
            break;
    }
    pAppParms->bDcmConfigure = EC_TRUE;

    // -ctloff
    if(GetCommandLineFlagInfo("ctloff" ,&info) && !info.is_default) {
        pAppParms->bDcmControlLoopDisabled = FLAGS_ctloff; // 将ctloff保存到bCtloff中
    }

    // -license
    OsSnprintf(pAppParms->szLicenseKey, sizeof(pAppParms->szLicenseKey) - 1,"%s", FLAGS_license.c_str()); // 将license保存到szLicenseKey中

    // -maxbusslaves
    if(GetCommandLineFlagInfo("maxbusslaves" ,&info) && !info.is_default) {
        pAppParms->dwMaxBusSlaves = FLAGS_maxbusslaves;
    }


    // -mbxsrv
    if(GetCommandLineFlagInfo("mbxsrv" ,&info) && !info.is_default) {
        pAppParms->wMbxGatewayServerPort = FLAGS_mbxsrv;
    }


    // link layer process
    EC_T_CHAR *szNextParm = ptcWord;
    EC_T_DWORD dwNewCycleDurationUsec = 0;

    EC_T_DWORD dwRes = EC_E_NOERROR;
    dwRes = CreateLinkParms(FLAGS_link, FLAGS_instance, FLAGS_mode, &pAppParms->apLinkParms[pAppParms->dwNumLinkLayer]);
    if (EC_E_NOERROR != dwRes) {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "SYNTAX_ERROR: %s!\n", szNextParm));
        dwRetVal = EC_E_INVALIDPARM;
        goto Exit;
    }
    pAppParms->dwNumLinkLayer++;

//    if (pAppParms->dwNumLinkLayer > 1) {
//        dwRetVal = EC_E_INVALIDPARM;
//        goto Exit;
//    } else {
//        EC_CPUSET_ZERO(CpuSet);
//        EC_CPUSET_SET(CpuSet, dwCpuIndex);
//        apLinkParms[dwNumLinkLayer]->dwIstPriority = (CpuSet << 16) | RECV_THREAD_PRIO;
//
//        pAppParms->dwNumLinkLayer++;
//
//        if (dwNewCycleDurationUsec != 0) {
//            pAppParms->dwBusCycleTimeUsec = dwNewCycleDurationUsec;
//        }
//    }

    if (bNextInstanceDetected)
    {
        dwRetVal = EC_E_BUSY;
    }
    else
    {
        dwRetVal = EC_E_NOERROR;
    }
Exit:
    if ((EC_E_NOERROR != dwRetVal) && (EC_E_BUSY != dwRetVal))
    {
//        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Full command line: %s\n", szCommandLine));
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Failed to parse command line parameters\n"));
    }
    return dwRetVal;
}


EC_T_DWORD PrepareCommandLine(EC_T_INT nArgc, EC_T_VOID* ppArgv, EC_T_CHAR* szCommandLine)
{
    EC_T_CHAR** ppcArgv = (EC_T_CHAR**)ppArgv;
    EC_T_CHAR* pcStrCur = szCommandLine;
    EC_T_INT   nStrRemain = COMMAND_LINE_BUFFER_LENGTH;
    EC_T_CHAR  szStrFormat[] = "%s";

    /* build szCommandLine from argument list, skipping executable name */
    for (nArgc--, ppcArgv++; nArgc > 0; nArgc--, ppcArgv++)
    {
        EC_T_BOOL bIsFileName = EC_FALSE;

        /* insert next argument */
        OsSnprintf(pcStrCur, nStrRemain - 1, szStrFormat, *ppcArgv);

        /* check for file name */
        if (0 == OsStrcmp(pcStrCur, "-f"))
        {
            bIsFileName = EC_TRUE;
        }
        /* adjust string cursor */
        nStrRemain -= (EC_T_INT)OsStrlen(pcStrCur);
        pcStrCur = pcStrCur + OsStrlen(pcStrCur);

        /* insert space */
        OsStrncpy(pcStrCur, " ", nStrRemain - 1); nStrRemain--; pcStrCur++;

        if (bIsFileName && (1 < nArgc))
        {
            /* move to next arg (ENI file name) */
            nArgc--; ppcArgv++;

            /* insert quotation mark */
            OsStrncpy(pcStrCur, "\"", nStrRemain - 1); nStrRemain--; pcStrCur++;

            /* insert ENI file name */
            OsSnprintf(pcStrCur, nStrRemain - 1, szStrFormat, *ppcArgv); nStrRemain -= (EC_T_INT)OsStrlen(pcStrCur);
            pcStrCur = pcStrCur + OsStrlen(pcStrCur);

            /* insert quotation mark */
            OsStrncpy(pcStrCur, "\" ", nStrRemain - 1); nStrRemain--; pcStrCur++;
        }
    }

    return EC_E_NOERROR;
}

/********************************************************************************/
/** \brief  signal handler.
*
* \return N/A
*/
static void SignalHandler(int nSignal)
{
    bRun = EC_FALSE;
}

/********************************************************************************/
/** Enable real-time environment
*
* Return: EC_E_NOERROR in case of success, EC_E_ERROR in case of failure.
*/
EC_T_DWORD EnableRealtimeEnvironment(EC_T_VOID)
{
    struct utsname SystemName;
    int nMaj, nMin, nSub;
    struct timespec ts;
    int nRetval;
    EC_T_DWORD dwResult = EC_E_ERROR;
    EC_T_BOOL bHighResTimerAvail;
    struct sched_param schedParam;

    /* master only tested on >= 2.6 kernel */
    nRetval = uname(&SystemName);
    if (nRetval != 0)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR calling uname(), required Linux kernel >= 2.6\n"));
        dwResult = EC_E_ERROR;
        goto Exit;
    }
    sscanf(SystemName.release, "%d.%d.%d", &nMaj, &nMin, &nSub);
    if (!(((nMaj == 2) && (nMin == 6)) || (nMaj >= 3)))
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR - detected kernel = %d.%d.%d, required Linux kernel >= 2.6\n", nMaj, nMin, nSub));
        dwResult = EC_E_ERROR;
        goto Exit;
    }

    /* request realtime scheduling for the current process
    * This value is overwritten for each individual task
    */
    schedParam.sched_priority = MAIN_THREAD_PRIO; /* 1 lowest priority, 99 highest priority */
    nRetval = sched_setscheduler(0, SCHED_FIFO, &schedParam);
    if (nRetval == -1)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR - cannot change scheduling policy!\n"
            "root privilege is required or realtime group has to be joined!\n"));
        goto Exit;
    }

    /* disable paging */
    nRetval = mlockall(MCL_CURRENT | MCL_FUTURE);
    if (nRetval == -1)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR - cannot disable paging!\n"));
        dwResult = EC_E_ERROR;
        goto Exit;
    }

    /* check if high resolution timers are available */
    if (clock_getres(CLOCK_MONOTONIC, &ts))
    {
        bHighResTimerAvail = EC_FALSE;
    }
    else
    {
        bHighResTimerAvail = !(ts.tv_sec != 0 || ts.tv_nsec != 1);
    }
    if (!bHighResTimerAvail)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "WARNING: High resolution timers not available\n"));
    }

    /* set type of OsSleep implementation  (eSLEEP_USLEEP, eSLEEP_NANOSLEEP or eSLEEP_CLOCK_NANOSLEEP) */
    OsSleepSetType(eSLEEP_CLOCK_NANOSLEEP);

    dwResult = EC_E_NOERROR;
Exit:
    return dwResult;
}

/** Show syntax
*
* Return: N/A
*/
static EC_T_VOID ShowSyntax(T_EC_DEMO_APP_CONTEXT* pAppContext)
{
    ShowSyntaxAppUsage(pAppContext);
    ShowSyntaxCommon(pAppContext);
    ShowSyntaxApp(pAppContext);
#if (!defined ECMASTERRAS_CLIENT)
    ShowSyntaxLinkLayer();
#endif
    return;
}

/********************************************************************************/
/** \brief  Demo Application entry point.
*
* \return  Value 0 is returned.
*/
int main(int nArgc, char* ppArgv[])
{
    EC_T_DWORD               dwRetVal = EC_E_ERROR;
    EC_T_DWORD               dwRes = EC_E_ERROR;
    EC_T_DWORD               dwIdx = 0;
    T_EC_DEMO_APP_CONTEXT    AppContext;
#if (defined INCLUDE_EC_LOGGING)
    CAtEmLogging             oLogging;
    EC_T_BOOL                bLogInitialized = EC_FALSE;
#endif
    EC_T_CHAR                szCommandLine[COMMAND_LINE_BUFFER_LENGTH];
    OsMemset(szCommandLine, '\0', COMMAND_LINE_BUFFER_LENGTH);

    OsMemset(&AppContext, 0, sizeof(AppContext));

    /* printf logging until logging initialized */
    AppContext.LogParms.dwLogLevel = EC_LOG_LEVEL_ERROR;
#if (defined INCLUDE_EC_LOGGING)
    AppContext.LogParms.pfLogMsg = CAtEmLogging::LogMsgOsPrintf;
#else
    AppContext.LogParms.pfLogMsg = EcDemoLogMsg;
#endif
    AppContext.LogParms.pLogContext = EC_NULL;
    OsMemcpy(G_pEcLogParms, &AppContext.LogParms, sizeof(EC_T_LOG_PARMS));

    AppContext.dwInstanceId = INSTANCE_MASTER_DEFAULT;

    ResetAppParms(&AppContext, &AppContext.AppParms);
    AppContext.AppParms.Os.dwSize = sizeof(EC_T_OS_PARMS);
    AppContext.AppParms.Os.dwSignature = EC_OS_PARMS_SIGNATURE;
    AppContext.AppParms.Os.dwSupportedFeatures = 0xFFFFFFFF;
    AppContext.AppParms.Os.PlatformParms.bConfigMutex = EC_TRUE;
    AppContext.AppParms.Os.PlatformParms.nMutexType = PTHREAD_MUTEX_RECURSIVE_NP;
    AppContext.AppParms.Os.PlatformParms.nMutexProtocol = PTHREAD_PRIO_NONE;
    OsInit(&AppContext.AppParms.Os);

    /* OS specific initialization */
    EnableRealtimeEnvironment();
    {
        sigset_t SigSet;
        int nSigNum = SIGALRM;
        sigemptyset(&SigSet);
        sigaddset(&SigSet, nSigNum);
        sigprocmask(SIG_BLOCK, &SigSet, NULL);
        signal(SIGINT, SignalHandler);
        signal(SIGTERM, SignalHandler);
    }
    /* set running flag */
    bRun = EC_TRUE;

    /* handle command line */
//    dwRes = PrepareCommandLine(nArgc, ppArgv, szCommandLine);
    dwRes = PrepareFlags(nArgc, ppArgv); // Modified by think 2024.03.02
    if (EC_E_NOERROR != dwRes)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Failed to prepare command line parameters\n"));
        dwRetVal = EC_E_ERROR;
        goto Exit;
    }
//    dwRes = SetAppParmsFromCommandLine(&AppContext, szCommandLine, &AppContext.AppParms);
    dwRes = SetAppParmsFromFlags(&AppContext, &AppContext.AppParms); // Modified by think 2024.03.02
    if (EC_E_NOERROR != dwRes)
    {
        dwRetVal = EC_E_INVALIDPARM;
        goto Exit;
    }
    /* initialize logging */
    if ((EC_LOG_LEVEL_SILENT != AppContext.AppParms.dwAppLogLevel) || (EC_LOG_LEVEL_SILENT != AppContext.AppParms.dwMasterLogLevel))
    {
#if (defined INCLUDE_EC_LOGGING)
        dwRes = oLogging.InitLogging(INSTANCE_MASTER_DEFAULT, LOG_ROLLOVER, LOG_THREAD_PRIO, AppContext.AppParms.CpuSet, AppContext.AppParms.szLogFileprefix, LOG_THREAD_STACKSIZE, AppContext.AppParms.dwLogBufferMaxMsgCnt);
        if (EC_E_NOERROR != dwRes)
        {
            dwRetVal = dwRes;
            goto Exit;
        }
        AppContext.LogParms.pfLogMsg = CAtEmLogging::LogMsg;
        AppContext.LogParms.pLogContext = (struct _EC_T_LOG_CONTEXT*)&oLogging;
        bLogInitialized = EC_TRUE;
#endif
        AppContext.LogParms.dwLogLevel = AppContext.AppParms.dwAppLogLevel;
    }
    else
    {
        AppContext.LogParms.dwLogLevel = EC_LOG_LEVEL_SILENT;
    }
    OsMemcpy(G_pEcLogParms, &AppContext.LogParms, sizeof(EC_T_LOG_PARMS));

    //EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "%s V%s for %s %s\n", EC_DEMO_APP_NAME, EC_FILEVERSIONSTR, ATECAT_PLATFORMSTR, EC_COPYRIGHT));
    //EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Full command line: %s\n", szCommandLine));

    /* adjust linklayer parameters */
    for (dwIdx = 0; dwIdx < MAX_LINKLAYER; dwIdx++)
    {
        EC_T_LINK_PARMS* pLinkParms = AppContext.AppParms.apLinkParms[dwIdx];
        if (EC_NULL == pLinkParms)
        {
            break;
        }
        EC_CPUSET_ZERO(pLinkParms->cpuIstCpuAffinityMask);
        if (!EC_CPUSET_IS_ZERO(AppContext.AppParms.CpuSet))
        {
            EC_CPUSET_SET(pLinkParms->cpuIstCpuAffinityMask, AppContext.AppParms.dwCpuIndex);
        }
        pLinkParms->dwIstPriority = RECV_THREAD_PRIO;

        OsMemcpy(&pLinkParms->LogParms, &AppContext.LogParms, sizeof(EC_T_LOG_PARMS));
        pLinkParms->LogParms.dwLogLevel = AppContext.AppParms.dwMasterLogLevel;
    }

#if (defined INCLUDE_EMLL_STATIC_LIBRARY)
    OsReplaceGetLinkLayerRegFunc(&DemoGetLinkLayerRegFunc);
#endif

    /* set CPU affinity */
    if (!EC_CPUSET_IS_ZERO(AppContext.AppParms.CpuSet))
    {
        dwRes = OsSetThreadAffinity(EC_NULL, AppContext.AppParms.CpuSet);
        if (EC_E_NOERROR != dwRes)
        {
            EcLogMsg(EC_LOG_LEVEL_CRITICAL, (pEcLogContext, EC_LOG_LEVEL_CRITICAL, "ERROR: Set Affinity Failed: %s (0x%lx))\n", ecatGetText(dwRes), dwRes));
            dwRetVal = dwRes;
            goto Exit;
        }
    }

    //! 这里就是进入主程序循环了 by think 2024.03.02
    if (EXECUTE_DEMOTIMINGTASK && IsLinkLayerTimingSet(AppContext.AppParms.apLinkParms))
    {
        CDemoLinkLayerTimingTask oDemoLinkLayerTimingTask(AppContext);
        dwRes = EcDemoApp(&AppContext);
    }
    else if(EXECUTE_DEMOTIMINGTASK)
    {
        CDemoTimingTaskPlatform oDemoTimingTaskPlatform = CDemoTimingTaskPlatform(AppContext);
        dwRes = oDemoTimingTaskPlatform.StartTimingTask(AppContext.AppParms.dwBusCycleTimeUsec * 1000);
        if (EC_E_NOERROR != dwRes)
        {
            EcLogMsg(EC_LOG_LEVEL_CRITICAL, (pEcLogContext, EC_LOG_LEVEL_CRITICAL, "ERROR starting timing task (StartTimingTask): %s (0x%lx))\n", ecatGetText(dwRes), dwRes));
            dwRetVal = dwRes;
            goto Exit;
        }
        dwRes = EcDemoApp(&AppContext);
    }
    else
    {
        dwRes = EcDemoApp(&AppContext);
    }

    if (EC_E_NOERROR != dwRes)
    {
        dwRetVal = dwRes;
        goto Exit;
    }
    /* no errors */
    dwRetVal = EC_E_NOERROR;

Exit:
    if (EC_E_INVALIDPARM == dwRetVal)
    {
//        ShowSyntax(&AppContext);
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "\033[31m\033[1mInvalid flags to start ROCOS-ECM.\033[0m\n" )); // Add by think 2024.03.04
    }

    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "%s stop.\n", EC_DEMO_APP_NAME));

    /* de-initialize message logging */
#if (defined INCLUDE_EC_LOGGING)
    if (bLogInitialized)
    {
        AppContext.LogParms.pfLogMsg = CAtEmLogging::LogMsgOsPrintf;
        AppContext.LogParms.pLogContext = EC_NULL;
        OsMemcpy(G_pEcLogParms, &AppContext.LogParms, sizeof(EC_T_LOG_PARMS));
        oLogging.DeinitLogging();
    }
#endif
    /* free app parameters */
    FreeAppParms(&AppContext, &AppContext.AppParms);

    return (EC_E_NOERROR == dwRetVal) ? 0 : -1;
}

/*-END OF SOURCE FILE--------------------------------------------------------*/
