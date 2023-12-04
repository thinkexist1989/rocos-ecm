/*-----------------------------------------------------------------------------
 * ecatProcess.cpp
 * Copyright                acontis technologies GmbH, Weingarten, Germany
 * Response                 Stefan Zintgraf
 * Description              EC-Master application
 * Modified                 Yang Luo , luoyang@sia.cn
 * Last Modification Date   2023.04.04 18:00
 *---------------------------------------------------------------------------*/

/*-INCLUDES------------------------------------------------------------------*/
#include "ecatProcess.h"

#ifndef INC_ATETHERCAT

#include "AtEthercat.h"

#endif

#ifdef ATEMRAS_SERVER

#include "AtEmRasSrv.h"

#endif

#include <cmath>
#include <cstring>
#include <termcolor/termcolor.hpp>
#include <EcFlags.h>

/*-License Key -------------------------------------------------------------*/
//#define LICENSE_KEY "FB3B37E5-014B554C-F51CA7DF"

/*-EtherCAT Configuration------------------------------------------------------*/
#include "include/rocos_ecm/ecat_config.h"

static EcatConfig *pEcatConfig = new EcatConfig();

/*-Logging-------------------------------------------------------------------*/
//#define DCM_ENABLE_LOGFILE

/*-LOCAL VARIABLES-----------------------------------------------------------*/
static EC_T_REGISTERRESULTS S_oRegisterResults; //
static T_EC_THREAD_PARAM S_DemoThreadParam;
static EC_T_PVOID S_pvtJobThread = EC_NULL;
#if (defined ATEMRAS_SERVER)
static EC_T_BOOL S_bRasSrvStarted = EC_FALSE;
static EC_T_PVOID S_pvRemoteApiSrvH = EC_NULL;
#endif
static EC_T_DCM_MODE S_eDcmMode = eDcmMode_Off;
static EC_T_BOOL S_bCtlOff = EC_FALSE;
static EC_T_CHAR *S_aszMeasInfo[MAX_JOB_NUM] =
        {
                (EC_T_CHAR *) "JOB_ProcessAllRxFrames",
                (EC_T_CHAR *) "JOB_SendAllCycFrames  ",
                (EC_T_CHAR *) "JOB_MasterTimer       ",
                (EC_T_CHAR *) "JOB_SendAcycFrames    ",
                (EC_T_CHAR *) "Cycle Time            ",
                (EC_T_CHAR *) "myAppWorkPd           ",
                (EC_T_CHAR *) "Write DCM logfile     "};

/*-FUNCTION DECLARATIONS-----------------------------------------------------*/
static EC_T_DWORD ecatNotifyCallback(EC_T_DWORD dwCode, EC_T_NOTIFYPARMS *pParms);

#if (defined ATEMRAS_SERVER)

static EC_T_DWORD RasNotifyWrapper(EC_T_DWORD dwCode, EC_T_NOTIFYPARMS *pParms);

#endif

static EC_T_VOID tEcJobTask(EC_T_VOID *pvThreadParamDesc);

/*-MYAPP---------------------------------------------------------------------*/
/* Demo code: Remove/change this in your application */
static EC_T_DWORD myAppInit(EC_T_PBYTE pbyConfig, T_EC_THREAD_PARAM *pEcThreadParam);

static EC_T_DWORD myAppPrepare(T_EC_THREAD_PARAM *pEcThreadParam);

static EC_T_DWORD myAppSetup(T_EC_THREAD_PARAM *pEcThreadParam);

static EC_T_DWORD myAppReadypd(T_EC_THREAD_PARAM *pEcThreadParam, EC_T_BYTE *pbyPDIn, EC_T_BYTE *pbyPDOut);

static EC_T_DWORD myAppWorkpd(T_EC_THREAD_PARAM *pEcThreadParam, EC_T_BYTE *pbyPDIn, EC_T_BYTE *pbyPDOut);

static EC_T_DWORD myAppDiagnosis(T_EC_THREAD_PARAM *pEcThreadParam);

static EC_T_DWORD myAppNotify(EC_T_DWORD dwCode, EC_T_NOTIFYPARMS *pParms);
/* Demo code: End */

/*-FUNCTION DEFINITIONS------------------------------------------------------*/

/** \brief EC-Master application process.
 *
 *
 * \return  Status value.
*/
EC_T_DWORD ecatProcess(
        EC_T_PBYTE pbyCnf /* [in]  Configuration file in YAML  */
        ,
        EC_T_CNF_TYPE eCnfType /* [in]  Enum type of configuration data provided */
        ,
        EC_T_PBYTE pbyEni /* [in]  Eni Configuration data */
        ,
        EC_T_DWORD dwCnfDataLen /* [in]  Length of configuration data in byte */
        ,
        EC_T_DWORD dwBusCycleTimeUsec /* [in]  bus cycle time in usec */
        ,
        EC_T_INT nVerbose /* [in]  verbosity level */
        ,
        EC_T_DWORD dwDuration /* [in]  test duration in msec (0 = forever) */
        ,
        EC_T_LINK_PARMS *poLinkParms /* [in]  pointer to link parameter */
        ,
        EC_T_VOID *pvTimingEvent /* [in]  Timing event handle */
#if (defined INCLUDE_TTS)
        ,
        EC_T_VOID *pvTtsEvent /* [in]  Timing event handle */
#endif
        ,
        EC_T_DWORD dwCpuIndex /* [in]  SMP only: CPU index */
        ,
        EC_T_BOOL bEnaPerfJobs /* [in]  Performance measurement */
        ,
        EC_T_INT nFlashAddress /* [in]  Flash: 0xFFFF: no, 0: all outputs, >0: slave address */
#if (defined ATEMRAS_SERVER)
        ,
        EC_T_WORD wServerPort /* [in]  Remote API Server Port */
#endif
#if (defined VLAN_FRAME_SUPPORT)
        ,
        EC_T_BOOL bVLANEnable /* [in]  VLAN is enabled */
        ,
        EC_T_WORD wVLANId /* [in]  VLAN ID */
        ,
        EC_T_BYTE byVLANPrio /* [in]  VLAN priority */
#endif
        ,
        EC_T_LINK_PARMS *poLinkParmsRed /* [in]  Redundancy Link Layer Parameter */
        ,
        EC_T_DCM_MODE eDcmMode /* [in]  DCM mode */
        ,
        EC_T_BOOL bCtlOff /* [in]  Disables / Enables the control loop for diagnosis */
        ,
        EC_T_OS_PARMS *poOsParms /* [in]  pointer to OS parameters  */
) {
    EC_T_DWORD dwRetVal = EC_E_NOERROR;
    EC_T_DWORD dwRes = EC_E_NOERROR;
    EC_T_BOOL bRes = EC_FALSE;
    T_EC_THREAD_PARAM *pEcThreadParam = &S_DemoThreadParam;
    CEcTimer oDcmStatusTimer;
    EC_T_BOOL bFirstDcmStatus = EC_TRUE;
    EC_T_CPUSET CpuSet;
    EC_CPUSET_ZERO(CpuSet);
    EC_CPUSET_SET(CpuSet, dwCpuIndex);

    /* store parameters */
    OsMemset(pEcThreadParam, 0, sizeof(T_EC_THREAD_PARAM));
    pEcThreadParam->pvTimingEvent = pvTimingEvent;
#if defined(INCLUDE_TTS)
    pEcThreadParam->pvTtsEvent = pvTtsEvent;
#endif
    OsMemcpy(&pEcThreadParam->LogParms, G_pEcLogParms, sizeof(EC_T_LOG_PARMS));
    pEcThreadParam->nVerbose = nVerbose;
    pEcThreadParam->dwCpuIndex = dwCpuIndex;
    pEcThreadParam->dwMasterID = INSTANCE_MASTER_DEFAULT;
    pEcThreadParam->dwBusCycleTimeUsec = dwBusCycleTimeUsec;
    pEcThreadParam->FlashData.nFlashAddress = nFlashAddress;

    S_eDcmMode = eDcmMode;
    S_bCtlOff = bCtlOff;

    /* check if interrupt mode is selected */
    if (poLinkParms->eLinkMode != EcLinkMode_POLLING) {
        dwRetVal = EC_E_INVALIDPARM;
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error: Link layer in 'interrupt' mode is not supported by EcMasterDemo. Please select 'polling' mode.\n"));
        goto Exit;
    }
    /* set thread affinity */
    {
        bRes = OsSetThreadAffinity(EC_NULL, CpuSet);
        if (!bRes) {
            dwRetVal = EC_E_INVALIDPARM;
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error: Set thread affinitiy, invalid CPU index %d\n", dwCpuIndex));
            goto Exit;
        }
    }
    /* create notification context */
    pEcThreadParam->pNotInst = EC_NEW(CEmNotification(pEcThreadParam->dwMasterID));
    if (EC_NULL == pEcThreadParam->pNotInst) {
        dwRetVal = EC_E_NOMEMORY;
        goto Exit;
    }
    if (bEnaPerfJobs) {
        PERF_MEASURE_JOBS_INIT(EC_NULL);
        EcLogMsg(EC_LOG_LEVEL_INFO,
                 (pEcLogContext, EC_LOG_LEVEL_INFO, "OsMeasGet100kHzFrequency(): %d MHz\n", OsMeasGet100kHzFrequency() /
                                                                                            10));
    }

    ////////////////        MY OWN CODE     ///////////////////////
    /* Initialize application */
    dwRes = myAppInit(pbyCnf, pEcThreadParam);
    if (EC_E_NOERROR != dwRes) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, (EC_T_CHAR *) "myAppInit failed: %s (0x%lx))\n", ecatGetText(
                         dwRes), dwRes));
        dwRetVal = dwRes;
        goto Exit;
    }
#ifdef ATEMRAS_SERVER
    /* start RAS server if enabled */
    if (0xFFFF != wServerPort) {
        ATEMRAS_T_SRVPARMS oRemoteApiConfig;

        OsMemset(&oRemoteApiConfig, 0, sizeof(ATEMRAS_T_SRVPARMS));
        oRemoteApiConfig.dwSignature = ATEMRASSRV_SIGNATURE;
        oRemoteApiConfig.dwSize = sizeof(ATEMRAS_T_SRVPARMS);
        oRemoteApiConfig.oAddr.dwAddr = 0; /* INADDR_ANY */
        oRemoteApiConfig.wPort = wServerPort;
        oRemoteApiConfig.wMaxClientCnt = 0;                                      /* Max. clients in parallel (0: unlimited) */
        oRemoteApiConfig.dwCycleTime = REMOTE_CYCLE_TIME;                        /* 2 msec */
        oRemoteApiConfig.dwWDTOLimit = (REMOTE_WD_TO_LIMIT / REMOTE_CYCLE_TIME); /* WD Timeout after 10secs */
        oRemoteApiConfig.dwReConTOLimit = 6000;                                  /* Reconnect Timeout after 6000*2msec + 10secs */
#if (defined LINUX) || (defined XENOMAI)
        oRemoteApiConfig.dwMasterPrio = (CpuSet << 16) | MAIN_THREAD_PRIO;
        oRemoteApiConfig.dwClientPrio = (CpuSet << 16) | MAIN_THREAD_PRIO;
#else
        oRemoteApiConfig.dwMasterPrio = MAIN_THREAD_PRIO;
        oRemoteApiConfig.dwClientPrio = MAIN_THREAD_PRIO;
#endif
        oRemoteApiConfig.pvNotifCtxt = pEcThreadParam->pNotInst; /* Notification context */
        oRemoteApiConfig.pfNotification = RasNotifyWrapper;      /* Notification function for emras Layer */
        oRemoteApiConfig.dwConcNotifyAmount = 100;               /* for the first pre-allocate 100 Notification spaces */
        oRemoteApiConfig.dwMbxNotifyAmount = 50;                 /* for the first pre-allocate 50 Notification spaces */
        oRemoteApiConfig.dwMbxUsrNotifySize = 3000;              /* 3K user space for Mailbox Notifications */
        oRemoteApiConfig.dwCycErrInterval = 500;                 /* span between to consecutive cyclic notifications of same type */
        if (1 <= nVerbose) {
            OsMemcpy(&oRemoteApiConfig.LogParms, G_pEcLogParms, sizeof(EC_T_LOG_PARMS));
            oRemoteApiConfig.LogParms.dwLogLevel = EC_LOG_LEVEL_ERROR;
        }
        EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Start Remote API Server now\n"));
        dwRes = emRasSrvStart(&oRemoteApiConfig, &S_pvRemoteApiSrvH);
        if (EC_E_NOERROR != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Cannot spawn Remote API Server\n"));
        }
        S_bRasSrvStarted = EC_TRUE;
    }
#endif
    /* Initialize EtherCAT master */
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "==========================\n"));
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Initialize EtherCAT Master\n"));
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "==========================\n"));
    {
        EC_T_INIT_MASTER_PARMS oInitParms;

        OsMemset(&oInitParms, 0, sizeof(EC_T_INIT_MASTER_PARMS));
        oInitParms.dwSignature = ATECAT_SIGNATURE;
        oInitParms.dwSize = sizeof(EC_T_INIT_MASTER_PARMS);
        oInitParms.pOsParms = poOsParms;
        oInitParms.pLinkParms = poLinkParms;
        oInitParms.pLinkParmsRed = poLinkParmsRed;
        oInitParms.dwBusCycleTimeUsec = dwBusCycleTimeUsec;
        oInitParms.dwMaxBusSlaves = MASTER_CFG_ECAT_MAX_BUS_SLAVES;
        oInitParms.dwMaxAcycFramesQueued = MASTER_CFG_MAX_ACYC_FRAMES_QUEUED;
        if (oInitParms.dwBusCycleTimeUsec >= 1000) {
            oInitParms.dwMaxAcycBytesPerCycle = MASTER_CFG_MAX_ACYC_BYTES_PER_CYC;
        } else {
            oInitParms.dwMaxAcycBytesPerCycle = 1500;
            oInitParms.dwMaxAcycFramesPerCycle = 1;
            oInitParms.dwMaxAcycCmdsPerCycle = 20;
        }
        oInitParms.dwEcatCmdMaxRetries = MASTER_CFG_MAX_ACYC_CMD_RETRIES;
        if (1 <= nVerbose) {
            OsMemcpy(&oInitParms.LogParms, G_pEcLogParms, sizeof(EC_T_LOG_PARMS));
            switch (nVerbose) {
                case 0:
                    oInitParms.LogParms.dwLogLevel = EC_LOG_LEVEL_SILENT;
                    break;
                case 1:
                    oInitParms.LogParms.dwLogLevel = EC_LOG_LEVEL_WARNING;
                    break;
                case 2:
                    oInitParms.LogParms.dwLogLevel = EC_LOG_LEVEL_WARNING;
                    break;
                case 3:
                    oInitParms.LogParms.dwLogLevel = EC_LOG_LEVEL_WARNING;
                    break;
                case 4:
                    oInitParms.LogParms.dwLogLevel = EC_LOG_LEVEL_INFO;
                    break;
                case 5:
                    oInitParms.LogParms.dwLogLevel = EC_LOG_LEVEL_VERBOSE;
                    break;
                default: /* no break */
                case 6:
                    oInitParms.LogParms.dwLogLevel = EC_LOG_LEVEL_VERBOSE_CYC;
                    break;
            }
        }
#if (defined VLAN_FRAME_SUPPORT)
        oInitParms.bVLANEnable = bVLANEnable;
        oInitParms.wVLANId = wVLANId;
        oInitParms.byVLANPrio = byVLANPrio;
#endif
        dwRes = ecatInitMaster(&oInitParms);
        if (dwRes != EC_E_NOERROR) {
            dwRetVal = dwRes;
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot initialize EtherCAT-Master: %s (0x%lx))\n", ecatGetText(
                             dwRes), dwRes));
            goto Exit;
        }
    }
    /* Print MAC address */
    if (nVerbose > 0) {
        ETHERNET_ADDRESS oSrcMacAddress;

        dwRes = ecatGetSrcMacAddress(&oSrcMacAddress);
        if (dwRes != EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot get MAC address: %s (0x%lx))\n", ecatGetText(
                             dwRes), dwRes));
        }
        EcLogMsg(EC_LOG_LEVEL_INFO,
                 (pEcLogContext, EC_LOG_LEVEL_INFO, "EtherCAT network adapter MAC: %02X-%02X-%02X-%02X-%02X-%02X\n",
                         oSrcMacAddress.b[0], oSrcMacAddress.b[1], oSrcMacAddress.b[2], oSrcMacAddress.b[3], oSrcMacAddress.b[4], oSrcMacAddress.b[5]));
    }
    /* Add License Key */
//    dwRes = ecatSetLicenseKey(LICENSE_KEY);
    //////////// MY OWN CODE /////////////////
    dwRes = ecatSetLicenseKey(const_cast<EC_T_CHAR *>(pEcatConfig->license.c_str()));
    if (dwRes != EC_E_NOERROR) {
        pEcatConfig->ecatInfo->isAuthorized = true; // ecMaster is authorized
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "\033[31m\033[1mThe license key: %s is not correct.\033[0m\n", pEcatConfig->license.c_str()));
    } else {
        pEcatConfig->ecatInfo->isAuthorized = false; // ecMaster is not authorized
        EcLogMsg(EC_LOG_LEVEL_INFO,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "\033[32m\033[1mThe license key: %s is correct.\033[0m\n", pEcatConfig->license.c_str()));
    }

    /* Create cyclic task to trigger master jobs */
    /*********************************************/
    {
        CEcTimer oTimeout;

        pEcThreadParam->bJobThreadRunning = EC_FALSE;
        pEcThreadParam->bJobThreadShutdown = EC_FALSE;
        S_pvtJobThread = OsCreateThread((EC_T_CHAR *) "tEcJobTask", tEcJobTask,
#if !(defined EC_VERSION_GO32)
                                        JOBS_THREAD_PRIO,
#else
                dwBusCycleTimeUsec,
#endif
                                        JOBS_THREAD_STACKSIZE, (EC_T_VOID *) pEcThreadParam);

        /* wait until thread is running */
        oTimeout.Start(2000);
        while (!oTimeout.IsElapsed() && !pEcThreadParam->bJobThreadRunning) {
            OsSleep(10);
        }
        if (!pEcThreadParam->bJobThreadRunning) {
            dwRetVal = EC_E_TIMEOUT;
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Timeout starting JobTask\n"));
            goto Exit;
        }
    }
    /* Configure master */
    dwRes = ecatConfigureMaster(eCnfType, pbyEni, dwCnfDataLen);
    if (dwRes != EC_E_NOERROR) {
        dwRetVal = dwRes;
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot configure EtherCAT-Master: %s (0x%lx))\n", ecatGetText(
                         dwRes), dwRes));
        goto Exit;
    }
    /* configure DC/DCM master is started with ENI */
    if (pbyEni != EC_NULL) {
        /* configure DC */
        {
            EC_T_DC_CONFIGURE oDcConfigure;

            OsMemset(&oDcConfigure, 0, sizeof(EC_T_DC_CONFIGURE));
            oDcConfigure.dwTimeout = ETHERCAT_DC_TIMEOUT;
            oDcConfigure.dwDevLimit = ETHERCAT_DC_DEV_LIMIT;
            oDcConfigure.dwSettleTime = ETHERCAT_DC_SETTLE_TIME;
            if (eDcmMode_MasterRefClock == eDcmMode) {
                oDcConfigure.dwTotalBurstLength = 10000;
                oDcConfigure.dwBurstBulk = 1;
            } else {
                oDcConfigure.dwTotalBurstLength = ETHERCAT_DC_ARMW_BURSTCYCLES;
                if (dwBusCycleTimeUsec < 1000) {
                    /* if the cycle time is below 1000 usec, we have to reduce the number of frames sent within one cycle */
                    oDcConfigure.dwBurstBulk = ETHERCAT_DC_ARMW_BURSTSPP / 2;
                } else {
                    oDcConfigure.dwBurstBulk = ETHERCAT_DC_ARMW_BURSTSPP;
                }
            }
#if (defined INCLUDE_DCX)
            if (eDcmMode_Dcx == S_eDcmMode) {
                oDcConfigure.bAcycDistributionDisabled = EC_FALSE; /* Enable acyclic distribution if cycle time is above 1000 usec to get DCX in sync */
            } else {
                oDcConfigure.bAcycDistributionDisabled = EC_TRUE;
            }
#else
            oDcConfigure.bAcycDistributionDisabled = EC_TRUE;
#endif
            dwRes = ecatDcConfigure(&oDcConfigure);
            if (dwRes != EC_E_NOERROR) {
                dwRetVal = dwRes;
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot configure DC! (Result = 0x%x)", dwRes));
                goto Exit;
            }
        }
        /* configure DCM */
        {
            EC_T_DCM_CONFIG oDcmConfig;
            EC_T_BOOL bLogEnabled = EC_FALSE;
            EC_T_INT nCtlSetVal =
                    ((dwBusCycleTimeUsec * 2) / 3) * 1000; /* set value in nanosec, 66% of the bus cycle */
#ifdef DCM_ENABLE_LOGFILE
            if (3 <= nVerbose) {
                bLogEnabled = EC_TRUE;
            }
#endif
            OsMemset(&oDcmConfig, 0, sizeof(EC_T_DCM_CONFIG));
            switch (eDcmMode) {
                case eDcmMode_Off:
                    oDcmConfig.eMode = eDcmMode_Off;
                    break;
                case eDcmMode_BusShift:
                    oDcmConfig.eMode = eDcmMode_BusShift;
                    oDcmConfig.u.BusShift.nCtlSetVal = nCtlSetVal;
                    oDcmConfig.u.BusShift.dwInSyncLimit =
                            (dwBusCycleTimeUsec * 1000) / 5; /* 20 % limit in nsec for InSync monitoring */
                    oDcmConfig.u.BusShift.bLogEnabled = bLogEnabled;
                    if (S_bCtlOff) {
                        EcLogMsg(EC_LOG_LEVEL_INFO,
                                 (pEcLogContext, EC_LOG_LEVEL_INFO, "DCM control loop disabled for diagnosis!\n"));
                        oDcmConfig.u.BusShift.bCtlOff = EC_TRUE;
                    }
                    break;
                case eDcmMode_MasterShift:
                    oDcmConfig.eMode = eDcmMode_MasterShift;
                    oDcmConfig.u.MasterShift.nCtlSetVal = nCtlSetVal;
                    oDcmConfig.u.MasterShift.dwInSyncLimit =
                            (dwBusCycleTimeUsec * 1000) / 5; /* 20 % limit in nsec for InSync monitoring */
                    oDcmConfig.u.MasterShift.bLogEnabled = bLogEnabled;
                    if (S_bCtlOff) {
                        EcLogMsg(EC_LOG_LEVEL_INFO,
                                 (pEcLogContext, EC_LOG_LEVEL_INFO, "DCM control loop disabled for diagnosis!\n"));
                        oDcmConfig.u.MasterShift.bCtlOff = EC_TRUE;
                    }
                    break;
                case eDcmMode_MasterRefClock:
                    oDcmConfig.eMode = eDcmMode_MasterRefClock;
                    oDcmConfig.u.MasterRefClock.nCtlSetVal = nCtlSetVal;
                    oDcmConfig.u.MasterRefClock.bLogEnabled = bLogEnabled;
                    break;
                case eDcmMode_LinkLayerRefClock:
                    oDcmConfig.eMode = eDcmMode_LinkLayerRefClock;
                    oDcmConfig.u.LinkLayerRefClock.nCtlSetVal = nCtlSetVal;
                    oDcmConfig.u.LinkLayerRefClock.bLogEnabled = bLogEnabled;
                    break;
#if (defined INCLUDE_DCX)
                case eDcmMode_Dcx:
                    oDcmConfig.eMode = eDcmMode_Dcx;
                    /* Mastershift */
                    oDcmConfig.u.Dcx.MasterShift.nCtlSetVal = nCtlSetVal;
                    oDcmConfig.u.Dcx.MasterShift.dwInSyncLimit =
                            (dwBusCycleTimeUsec * 1000) / 5; /* 20 % limit in nsec for InSync monitoring */
                    oDcmConfig.u.Dcx.MasterShift.bLogEnabled = bLogEnabled;
                    /* Dcx Busshift */
                    oDcmConfig.u.Dcx.nCtlSetVal = nCtlSetVal;
                    oDcmConfig.u.Dcx.dwInSyncLimit =
                            (dwBusCycleTimeUsec * 1000) / 5; /* 20 % limit in nsec for InSync monitoring */
                    oDcmConfig.u.Dcx.bLogEnabled = bLogEnabled;
                    oDcmConfig.u.Dcx.dwExtClockTimeout = 1000;
                    oDcmConfig.u.Dcx.wExtClockFixedAddr = 0; /* 0 only when clock adjustment in external mode configured by EcEngineer */
                    if (S_bCtlOff) {
                        EcLogMsg(EC_LOG_LEVEL_INFO,
                                 (pEcLogContext, EC_LOG_LEVEL_INFO, "DCM control loop disabled for diagnosis!\n"));

                        oDcmConfig.u.Dcx.MasterShift.bCtlOff = EC_TRUE;
                        oDcmConfig.u.Dcx.bCtlOff = EC_TRUE;
                    }
                    break;
#endif
                default:
                    dwRetVal = EC_E_NOTSUPPORTED;
                    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "DCM mode is not supported!"));
                    goto Exit;
            }
            dwRes = ecatDcmConfigure(&oDcmConfig, 0);
            switch (dwRes) {
                case EC_E_NOERROR:
                    break;
                case EC_E_FEATURE_DISABLED:
                    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot configure DCM mode!"));
                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "Start with -dcmmode off to run the DC demo without DCM, or prepare the ENI file to support the requested DCM mode"));
                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "In ET9000 for example, select under "
                                                                 "Advanced settings\\Distributed clocks"
                                                                 " "
                                                                 "DC in use"
                                                                 " and "
                                                                 "Slave Mode"
                                                                 ""));
                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "to support BusShift and MasterRefClock modes."));
                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "Please refer to the class A manual for further information"));
                    dwRetVal = dwRes;
                    goto Exit;
                default:
                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot configure DCM mode! %s (Result = 0x%x)", ecatGetText(
                                     dwRes), dwRes));
                    dwRetVal = dwRes;
                    goto Exit;
            }
        }
    }
    /* Register client */
    OsMemset(&S_oRegisterResults, 0, sizeof(EC_T_REGISTERRESULTS));
    dwRes = ecatRegisterClient(ecatNotifyCallback, pEcThreadParam, &S_oRegisterResults);
    if (dwRes != EC_E_NOERROR) {
        dwRetVal = dwRes;
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot register client: %s (0x%lx))\n", ecatGetText(
                         dwRes), dwRes));
        goto Exit;
    }
    pEcThreadParam->pNotInst->SetClientID(S_oRegisterResults.dwClntId);

    /* Print found slaves */
    if (nVerbose >= 2) {
        dwRes = ecatScanBus(ETHERCAT_SCANBUS_TIMEOUT);
        pEcThreadParam->pNotInst->ProcessNotificationJobs();
        switch (dwRes) {
            case EC_E_NOERROR:
            case EC_E_BUSCONFIG_MISMATCH:
            case EC_E_LINE_CROSSED:
                PrintSlaveInfos(pEcThreadParam);
                break;
            default:
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot scan bus: %s (0x%lx)\n", ecatGetText(
                                 dwRes), dwRes));
                break;
        }
        if (dwRes != EC_E_NOERROR) {
            dwRetVal = dwRes;
            goto Exit;
        }
    }

    //////////// MY OWN CODE /////////////////
    // 在ecatConfigureMaster之后，ecatStart之前处理共享内存映射
    /* 获取Process Data Memory Size */
    EC_T_MEMREQ_DESC MemReqDesc;
    EC_T_DWORD dwNumOutData;

    EC_T_IOCTLPARMS IoCtlParams;
    IoCtlParams.pbyInBuf    = EC_NULL;
    IoCtlParams.dwInBufSize = 0;
    IoCtlParams.pbyOutBuf = (EC_T_BYTE*)&MemReqDesc;
    IoCtlParams.dwOutBufSize = sizeof(MemReqDesc);
    IoCtlParams.pdwNumOutData = &dwNumOutData;

    dwRes = ecatIoControl(EC_IOCTL_GET_PDMEMORYSIZE, &IoCtlParams);

    if(dwRes != EC_E_NOERROR) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot get process data memory size\n", dwRes));
        dwRetVal = dwRes;
        goto Exit;
    } else {
        EcLogMsg(EC_LOG_LEVEL_INFO,
                 (pEcLogContext, EC_LOG_LEVEL_INFO, "******************************************************************************\n", MemReqDesc.dwPDInSize));

        EcLogMsg(EC_LOG_LEVEL_INFO,
                 (pEcLogContext, EC_LOG_LEVEL_INFO, "\033[34m\033[1mProcess data input memory size: %d\033[0m\n", MemReqDesc.dwPDInSize));
        EcLogMsg(EC_LOG_LEVEL_INFO,
                 (pEcLogContext, EC_LOG_LEVEL_INFO, "\033[34m\033[1mProcess data output memory size: %d\033[0m\n", MemReqDesc.dwPDOutSize));
    }

    /* 创建PD Memory */
    pEcatConfig->createPdDataMemoryProvider(MemReqDesc.dwPDInSize, MemReqDesc.dwPDOutSize);


    /* 配置Memory Provider */
    EC_T_MEMPROV_DESC MemProvDesc;
    MemProvDesc.pvContext = EC_NULL; // 由于不使用callback function，不需要pvContext

    MemProvDesc.pbyPDOutData = (EC_T_PBYTE)(pEcatConfig->pdOutputPtr); // PD OUT的内存指针
    MemProvDesc.dwPDOutDataLength = MemReqDesc.dwPDOutSize;
    MemProvDesc.pbyPDInData = (EC_T_PBYTE)(pEcatConfig->pdInputPtr); // PD IN的内存指针
    MemProvDesc.dwPDInDataLength = MemReqDesc.dwPDInSize;

    MemProvDesc.pfPDOutDataReadRequest = EC_NULL; // PD OUT的读取请求回调函数
    MemProvDesc.pfPDOutDataReadRelease = EC_NULL; // PD OUT的读取释放回调函数
    MemProvDesc.pfPDInDataWriteRequest = EC_NULL; // PD IN的写入请求回调函数
    MemProvDesc.pfPDInDataWriteRelease = EC_NULL; // PD IN的写入释放回调函数



    EC_T_IOCTLPARMS IoCtlPdMemProvParams;
    IoCtlPdMemProvParams.pbyInBuf    = (EC_T_BYTE*)&MemProvDesc;
    IoCtlPdMemProvParams.dwInBufSize = sizeof(MemProvDesc);
    IoCtlPdMemProvParams.pbyOutBuf = EC_NULL;
    IoCtlPdMemProvParams.dwOutBufSize = 0;
    IoCtlPdMemProvParams.pdwNumOutData = EC_NULL;

    dwRes = ecatIoControl(EC_IOCTL_REGISTER_PDMEMORYPROVIDER, &IoCtlPdMemProvParams);

    if(dwRes != EC_E_NOERROR) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "\033[31m\033[1mCannot register process data memory provider\033[0m\n", dwRes));
        dwRetVal = dwRes;
        goto Exit;
    } else {
        EcLogMsg(EC_LOG_LEVEL_INFO,
                 (pEcLogContext, EC_LOG_LEVEL_INFO, "\033[32m\033[1mRegister process data memory provider successfully\033[0m\n"));
    }

    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "=====================\n"));
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Start EtherCAT Master\n"));
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "=====================\n"));


    while (bRun) { //while process
        pEcatConfig->ecatInfo->ecatState = static_cast<EcatInfo::EcatState>(ecatGetMasterState()); // Get Ec Master State
        //! Process EtherCAT state machine
        switch (pEcatConfig->ecatInfo->ecatRequestState) {
            case EcatInfo::INIT:
                pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::INIT;
                break;
            case EcatInfo::PREOP:
                switch (pEcatConfig->ecatInfo->ecatState) {
                    case EcatInfo::UNKNOWN:
                    case EcatInfo::BOOTSTRAP:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::INIT;
                        break;
                    default:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::PREOP;
                        break;
                }
                break;
            case EcatInfo::SAFEOP:
                switch (pEcatConfig->ecatInfo->ecatState) {
                    case EcatInfo::INIT:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::PREOP;
                        break;
                    case EcatInfo::UNKNOWN:
                    case EcatInfo::BOOTSTRAP:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::INIT;
                        break;
                    case EcatInfo::PREOP:
                    case EcatInfo::OP:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::SAFEOP;
                        break;
                    default:
                        break;
                }
                break;
            case EcatInfo::OP:
                switch (pEcatConfig->ecatInfo->ecatState) {
                    case EcatInfo::INIT:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::PREOP;
                        break;
                    case EcatInfo::UNKNOWN:
                    case EcatInfo::BOOTSTRAP:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::INIT;
                        break;
                    case EcatInfo::PREOP:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::SAFEOP;
                    case EcatInfo::SAFEOP:
                        pEcatConfig->ecatInfo->ecatNextExpectedState = EcatInfo::OP;
                        break;
                    default:
                        break;
                }
                break;
            case EcatInfo::BOOTSTRAP:
            case EcatInfo::UNKNOWN:
            default:
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Invaid Request Master State!\n"));
                break;
        }


        // Dive into state switch
        if(pEcatConfig->ecatInfo->ecatNextExpectedState == EcatInfo::INIT) { // set state to INIT
            /* set master and bus state to INIT */
            dwRes = ecatSetMasterState(ETHERCAT_STATE_CHANGE_TIMEOUT, eEcatState_INIT); // 切换到Init状态  by think
            pEcThreadParam->pNotInst->ProcessNotificationJobs();
            if (dwRes != EC_E_NOERROR) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot start set master state to INIT: %s (0x%lx))\n", ecatGetText(
                                 dwRes), dwRes));
                dwRetVal = dwRes;
                goto Exit;
            }

            //////////////    MY OWN CODE     /////////////////
            ///////////// Prepare Application /////////////////
            dwRes = myAppPrepare(pEcThreadParam); // 判断ecat_config.yaml中的slave数量与eni.xml中的是否相同 by think
            if (EC_E_NOERROR != dwRes) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, (EC_T_CHAR *) "myAppPrepare failed: %s (0x%lx))\n", ecatGetText(
                                 dwRes), dwRes));
                dwRetVal = dwRes;
                goto Exit;
            }

        }
        else if(pEcatConfig->ecatInfo->ecatNextExpectedState == EcatInfo::PREOP) { // set state to PREOP
            /* set master and bus state to PREOP */
            dwRes = ecatSetMasterState(ETHERCAT_STATE_CHANGE_TIMEOUT, eEcatState_PREOP); // 切换到Preop状态  by think
            pEcThreadParam->pNotInst->ProcessNotificationJobs();
            if (dwRes != EC_E_NOERROR) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot start set master state to PREOP: %s (0x%lx))\n", ecatGetText(
                                 dwRes), dwRes));
                dwRetVal = dwRes;
                goto Exit;
            }

            /////////////    MY OWN CODE     /////////////////
            ///////////// Setup Application /////////////////
            dwRes = myAppSetup(pEcThreadParam); // mapping相应的变量指针到PDO by think
            if (EC_E_NOERROR != dwRes) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, (EC_T_CHAR *) "myAppSetup failed: %s (0x%lx))\n", ecatGetText(
                                 dwRes), dwRes));
                dwRetVal = dwRes;
                goto Exit;
            }

        }
        else if(pEcatConfig->ecatInfo->ecatNextExpectedState == EcatInfo::SAFEOP) { // set state to SAFEOP
            /* set master and bus state to SAFEOP */
            dwRes = ecatSetMasterState(ETHERCAT_STATE_CHANGE_TIMEOUT, eEcatState_SAFEOP); // 切换到Safeop状态  by think
            pEcThreadParam->pNotInst->ProcessNotificationJobs();
            if (dwRes != EC_E_NOERROR) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot start set master state to SAFEOP: %s (0x%lx))\n", ecatGetText(
                                 dwRes), dwRes));

                /* most of the time SAFEOP is not reachable due to DCM */
                if ((eDcmMode_Off != eDcmMode) && (eDcmMode_LinkLayerRefClock != eDcmMode)) {
                    EC_T_DWORD dwStatus = 0;
                    EC_T_INT nDiffCur = 0, nDiffAvg = 0, nDiffMax = 0;

                    dwRes = ecatDcmGetStatus(&dwStatus, &nDiffCur, &nDiffAvg, &nDiffMax);
                    if (dwRes == EC_E_NOERROR) {
                        if (dwStatus != EC_E_NOERROR) {
                            EcLogMsg(EC_LOG_LEVEL_ERROR,
                                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "DCM Status: %s (0x%08X)\n", ecatGetText(
                                             dwStatus), dwStatus));
                        }
                    } else {
                        EcLogMsg(EC_LOG_LEVEL_ERROR,
                                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot get DCM status! %s (0x%08X)\n", ecatGetText(
                                         dwRes), dwRes));
                    }
                }
                dwRetVal = dwRes;
                goto Exit;
            }

        }
        else if(pEcatConfig->ecatInfo->ecatNextExpectedState == EcatInfo::OP) { // set state to OP
            /* set master and bus state to OP */
            dwRes = ecatSetMasterState(ETHERCAT_STATE_CHANGE_TIMEOUT, eEcatState_OP); // 切换到Op状态 by think
            pEcThreadParam->pNotInst->ProcessNotificationJobs();
            if (dwRes != EC_E_NOERROR) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot start set master state to OP: %s (0x%lx))\n", ecatGetText(
                                 dwRes), dwRes));
                dwRetVal = dwRes;
                goto Exit;
            }

            if (pEcThreadParam->TscMeasDesc.bMeasEnabled) {
                EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "\n"));
                EcLogMsg(EC_LOG_LEVEL_INFO,
                         (pEcLogContext, EC_LOG_LEVEL_INFO, "Job times during startup <INIT> to <%s>:\n", ecatStateToStr(
                                 ecatGetMasterState())));
                PERF_MEASURE_JOBS_SHOW(); /* show job times */
                EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "\n"));
                ecatPerfMeasReset(&pEcThreadParam->TscMeasDesc, 0xFFFFFFFF); /* clear job times of startup phase */
            }

#if (defined DEBUG) && (defined XENOMAI)
            /* Enabling mode switch warnings for shadowed task */
        dwRes = rt_task_set_mode(0, T_WARNSW, NULL);
        if (0 != dwRes)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "EnableRealtimeEnvironment: rt_task_set_mode returned error 0x%lx\n", dwRes));
            OsDbgAssert(EC_FALSE);
        }
#endif

            /* run the demo */
            if (dwDuration != 0) {
                pEcThreadParam->oDuration.Start(dwDuration);
            }

            while ((pEcatConfig->ecatInfo->ecatRequestState == EcatInfo::OP) && bRun && (!pEcThreadParam->oDuration.IsStarted() ||
                                                                                  !pEcThreadParam->oDuration.IsElapsed())) { // while op 其实运行到这里时，实时任务已经在tEcJobTask中运行了，这里只是在运行过程中做一些检查
                if (nVerbose >= 2) {
                    PERF_MEASURE_JOBS_SHOW(); /* show job times */
                }
                bRun = !OsTerminateAppRequest(); /* check if demo shall terminate */

                /*****************************************************************************************/
                /* Demo code: Remove/change this in your application: Do some diagnosis outside job task */
                /*****************************************************************************************/
                myAppDiagnosis(pEcThreadParam);

                if (pbyEni != EC_NULL) {
                    if ((eDcmMode_Off != S_eDcmMode) && (eDcmMode_LinkLayerRefClock != S_eDcmMode)) {
                        EC_T_DWORD dwStatus = 0;
                        EC_T_BOOL bWriteDiffLog = EC_FALSE;
                        EC_T_INT nDiffCur = 0, nDiffAvg = 0, nDiffMax = 0;

                        if (!oDcmStatusTimer.IsStarted() || oDcmStatusTimer.IsElapsed()) {
                            bWriteDiffLog = EC_TRUE;
                            oDcmStatusTimer.Start(5000);
                        }

                        dwRes = ecatDcmGetStatus(&dwStatus, &nDiffCur, &nDiffAvg, &nDiffMax);
                        if (dwRes == EC_E_NOERROR) {
                            if (bFirstDcmStatus) {
                                EcLogMsg(EC_LOG_LEVEL_INFO,
                                         (pEcLogContext, EC_LOG_LEVEL_INFO, "DCM during startup (<INIT> to <%s>)", ecatStateToStr(
                                                 ecatGetMasterState())));
                            }
                            if ((dwStatus != EC_E_NOTREADY) && (dwStatus != EC_E_BUSY) && (dwStatus != EC_E_NOERROR)) {
                                EcLogMsg(EC_LOG_LEVEL_INFO,
                                         (pEcLogContext, EC_LOG_LEVEL_INFO, "DCM Status: %s (0x%08X)\n", ecatGetText(
                                                 dwStatus), dwStatus));
                            }
                            if (bWriteDiffLog && (nVerbose >= 3)) {
                                EcLogMsg(EC_LOG_LEVEL_INFO,
                                         (pEcLogContext, EC_LOG_LEVEL_INFO, "DCM Diff(ns): Cur=%7d, Avg=%7d, Max=%7d", nDiffCur, nDiffAvg, nDiffMax));
                            }
                        } else {
                            if ((eEcatState_OP == ecatGetMasterState()) || (eEcatState_SAFEOP == ecatGetMasterState())) {
                                EcLogMsg(EC_LOG_LEVEL_ERROR,
                                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot get DCM status! %s (0x%08X)\n", ecatGetText(
                                                 dwRes), dwRes));
                            }
                        }
#if (defined INCLUDE_DCX)
                        if (eDcmMode_Dcx == S_eDcmMode && EC_E_NOERROR == dwRes) {
                            EC_T_INT64 nTimeStampDiff = 0;
                            dwRes = ecatDcxGetStatus(&dwStatus, &nDiffCur, &nDiffAvg, &nDiffMax, &nTimeStampDiff);
                            if (EC_E_NOERROR == dwRes) {
                                if (bFirstDcmStatus) {
                                    EcLogMsg(EC_LOG_LEVEL_INFO,
                                             (pEcLogContext, EC_LOG_LEVEL_INFO, "DCX during startup (<INIT> to <%s>)", ecatStateToStr(
                                                     ecatGetMasterState())));
                                }
                                if ((dwStatus != EC_E_NOTREADY) && (dwStatus != EC_E_BUSY) && (dwStatus != EC_E_NOERROR)) {
                                    EcLogMsg(EC_LOG_LEVEL_INFO,
                                             (pEcLogContext, EC_LOG_LEVEL_INFO, "DCX Status: %s (0x%08X)\n", ecatGetText(
                                                     dwStatus), dwStatus));
                                }
                                if (bWriteDiffLog && (nVerbose >= 3)) {
                                    EcLogMsg(EC_LOG_LEVEL_INFO,
                                             (pEcLogContext, EC_LOG_LEVEL_INFO, "DCX Diff(ns): Cur=%7d, Avg=%7d, Max=%7d, TimeStamp=%7d", nDiffCur, nDiffAvg, nDiffMax, nTimeStampDiff));
                                }
                            } else {
                                if ((eEcatState_OP == ecatGetMasterState()) ||
                                    (eEcatState_SAFEOP == ecatGetMasterState())) {
                                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot get DCX status! %s (0x%08X)\n", ecatGetText(
                                                     dwRes), dwRes));
                                }
                            }
                        }
#endif
                        if (bFirstDcmStatus && (EC_E_NOERROR == dwRes)) {
                            bFirstDcmStatus = EC_FALSE;
#if (defined ATECAT_VERSION) && (ATECAT_VERSION >= 0x02040106)
                            ecatDcmResetStatus();
#endif
                        }
                    }
                }
                /* process notification jobs */
                pEcThreadParam->pNotInst->ProcessNotificationJobs();

                OsSleep(5);
            } // end while op

        }

        OsSleep(50);
    } // end while process

    if (pEcThreadParam->TscMeasDesc.bMeasEnabled) {
        EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "\n"));
        EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Job times before shutdown\n"));
        PERF_MEASURE_JOBS_SHOW(); /* show job times */
    }

    /* stop EtherCAT-Master */
    Exit:
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "========================\n"));
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Shutdown EtherCAT Master\n"));
    EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "========================\n"));

    /* Set Master state back to INIT */
    if (eEcatState_UNKNOWN != ecatGetMasterState()) {
        dwRes = ecatSetMasterState(ETHERCAT_STATE_CHANGE_TIMEOUT, eEcatState_INIT);
        if (EC_E_NOERROR != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot stop EtherCAT-Master: %s (0x%lx))\n", ecatGetText(
                             dwRes), dwRes));
        }
    }
    /* Unregister client */
    if (S_oRegisterResults.dwClntId != 0) {
        dwRes = ecatUnregisterClient(S_oRegisterResults.dwClntId);
        if (EC_E_NOERROR != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot unregister client: %s (0x%lx))\n", ecatGetText(
                             dwRes), dwRes));
        }
        S_oRegisterResults.dwClntId = 0;
    }
#if (defined DEBUG) && (defined XENOMAI)
    /* Disable PRIMARY to SECONDARY MODE switch warning */
    dwRes = rt_task_set_mode(T_WARNSW, 0, NULL);
    if (dwRes != 0)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "OsCreateThread: rt_task_set_mode returned error %d\n", dwRes));
        OsDbgAssert(EC_FALSE);
    }
#endif
    /* Shutdown tEcJobTask */
    {
        CEcTimer oTimeout;
        pEcThreadParam->bJobThreadShutdown = EC_TRUE;
        oTimeout.Start(2000);
        while (pEcThreadParam->bJobThreadRunning && !oTimeout.IsElapsed()) {
            OsSleep(10);
        }
        if (S_pvtJobThread != EC_NULL) {
            OsDeleteThreadHandle(S_pvtJobThread);
            S_pvtJobThread = EC_NULL;
        }
    }
    /* Deinitialize master */
    dwRes = ecatDeinitMaster();
    if (EC_E_NOERROR != dwRes) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Cannot de-initialize EtherCAT-Master: %s (0x%lx)\n", ecatGetText(
                         dwRes), dwRes));
    }
#ifdef ATEMRAS_SERVER
    /* Stop RAS server */
    if (S_bRasSrvStarted) {
        EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Stop Remote Api Server\n"));

        if (EC_E_NOERROR != emRasSrvStop(S_pvRemoteApiSrvH, 2000)) {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Remote API Server shutdown failed\n"));
        }
    }
#endif
    if (pEcThreadParam->TscMeasDesc.bMeasEnabled) {
        PERF_MEASURE_JOBS_DEINIT();
    }
    /* delete notification context */
    SafeDelete(pEcThreadParam->pNotInst);

    /* free flash buffer */
    SafeOsFree(pEcThreadParam->FlashData.pbyFlashBuf);

    return dwRetVal;
}

#if defined(INCLUDE_TTS)
/********************************************************************************/
/** \brief  Callback function for TTS feature.
 *          This function should be as quick as possible, because it is called
 *          from a ISR function in LinkLayer.
*
* \return N/A
*/
EC_T_VOID cbTtsStartCycle(EC_T_VOID *pvTtsEvent)
{
    OsSetEvent(pvTtsEvent);
}
#endif

/********************************************************************************/
/** \brief  Trigger jobs to drive master, and update process data.
*
* \return N/A
*/
static EC_T_VOID tEcJobTask(EC_T_VOID *pvThreadParamDesc) {
    EC_T_DWORD dwRes = EC_E_ERROR;
    T_EC_THREAD_PARAM *pEcThreadParam = (T_EC_THREAD_PARAM *) pvThreadParamDesc;
    EC_T_CPUSET CpuSet;
    EC_T_INT nOverloadCounter = 0; /* counter to check if cycle time is to short */
    EC_T_BOOL bOk;
    EC_T_USER_JOB_PARMS oJobParms;
    OsMemset(&oJobParms, 0, sizeof(EC_T_USER_JOB_PARMS));

    EC_CPUSET_ZERO(CpuSet);
    EC_CPUSET_SET(CpuSet, pEcThreadParam->dwCpuIndex);
    bOk = OsSetThreadAffinity(EC_NULL, CpuSet);
    if (!bOk) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error: Set job task affinitiy, invalid CPU index %d\n", pEcThreadParam->dwCpuIndex));
        goto Exit;
    }

    // Op 模式下的循环PDO处理就在这里了
    /* demo loop */
    pEcThreadParam->bJobThreadRunning = EC_TRUE;
    do {
#if defined(INCLUDE_TTS)
        OsWaitForEvent(pEcThreadParam->pvTtsEvent, EC_WAITINFINITE);
#else
        /* wait for next cycle (event from scheduler task) */
        OsWaitForEvent(pEcThreadParam->pvTimingEvent, EC_WAITINFINITE); // 阻塞等待定时器事件
#endif

        PERF_JOB_END(PERF_CycleTime);
        ////////===========My Own Code============/////////
        pEcatConfig->ecatInfo->minCyclcTime = pEcThreadParam->TscMeasDesc.aTscTime[PERF_CycleTime].dwMin / 10.0;
        pEcatConfig->ecatInfo->maxCycleTime = pEcThreadParam->TscMeasDesc.aTscTime[PERF_CycleTime].dwMax / 10.0;
        pEcatConfig->ecatInfo->avgCycleTime = pEcThreadParam->TscMeasDesc.aTscTime[PERF_CycleTime].dwAvg / 1600.0;
        pEcatConfig->ecatInfo->currCycleTime = pEcThreadParam->TscMeasDesc.aTscTime[PERF_CycleTime].dwCurr / 10.0;

        //////////////////////////////////////////////////
        PERF_JOB_START(PERF_CycleTime);

        /* process all received frames (read new input values) */
        PERF_JOB_START(JOB_ProcessAllRxFrames);
        dwRes = ecatExecJob(eUsrJob_ProcessAllRxFrames, &oJobParms);
        if (EC_E_NOERROR != dwRes && EC_E_INVALIDSTATE != dwRes && EC_E_LINK_DISCONNECTED != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: ecatExecJob( eUsrJob_ProcessAllRxFrames): %s (0x%lx)\n", ecatGetText(
                             dwRes), dwRes));
        }
        PERF_JOB_END(JOB_ProcessAllRxFrames);

        if (EC_E_NOERROR == dwRes) {
            if (!oJobParms.bAllCycFramesProcessed) {
                /* it is not reasonable, that more than 5 continuous frames are lost */
                nOverloadCounter += 10;
                if (nOverloadCounter >= 50) {
                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "Error: System overload: Cycle time too short or huge jitter!\n"));
                } else {
                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "eUsrJob_ProcessAllRxFrames - not all previously sent frames are received/processed (frame loss)!\n"));
                }
                if (pEcThreadParam->TscMeasDesc.bMeasEnabled) {
                    EcLogMsg(EC_LOG_LEVEL_INFO,
                             (pEcLogContext, EC_LOG_LEVEL_INFO, "PerfMsmt '%s' (current) [usec]: %3d.%d\n", S_aszMeasInfo[PERF_CycleTime],
                                     pEcThreadParam->TscMeasDesc.aTscTime[PERF_CycleTime].dwCurr / 10,
                                     pEcThreadParam->TscMeasDesc.aTscTime[PERF_CycleTime].dwCurr % 10));
                }
            } else {
                /* everything o.k.? If yes, decrement overload counter */
                if (nOverloadCounter > 0)
                    nOverloadCounter--;
            }
        }
            /* Handle DCM logging                                    */
#ifdef DCM_ENABLE_LOGFILE
        PERF_JOB_START(PERF_DCM_Logfile);
        {
            EC_T_CHAR *pszLog = EC_NULL;

            ecatDcmGetLog(&pszLog);
            if ((EC_NULL != pszLog)) {
                ((CAtEmLogging *) pEcLogContext)->LogDcm(pszLog);
            }
        }
        PERF_JOB_END(PERF_DCM_Logfile);
#endif
        /*****************************************************/
        /* Demo code: Remove/change this in your application: Working process data cyclic call */
        /*****************************************************/
        PERF_JOB_START(PERF_myAppWorkpd);
        {
            EC_T_BYTE *abyPdIn = ecatGetProcessImageInputPtr();
            EC_T_BYTE *abyPdOut = ecatGetProcessImageOutputPtr();
            EC_T_STATE eMasterState = ecatGetMasterState();

            /***********Record Ec-Master State**************/
            pEcatConfig->ecatInfo->ecatState = static_cast<EcatInfo::EcatState>(eMasterState);

            //            if ((eEcatState_SAFEOP == eMasterState) || (eEcatState_OP == eMasterState))
            if (eEcatState_OP == eMasterState) {
                myAppWorkpd(pEcThreadParam, abyPdIn, abyPdOut);
            } else if (eEcatState_SAFEOP == eMasterState) {
                myAppReadypd(pEcThreadParam, abyPdIn, abyPdOut);
            }
        }
        PERF_JOB_END(PERF_myAppWorkpd);

        /* write output values of current cycle, by sending all cyclic frames */
        PERF_JOB_START(JOB_SendAllCycFrames);
        dwRes = ecatExecJob(eUsrJob_SendAllCycFrames, &oJobParms);
        if (EC_E_NOERROR != dwRes && EC_E_INVALIDSTATE != dwRes && EC_E_LINK_DISCONNECTED != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatExecJob( eUsrJob_SendAllCycFrames,    EC_NULL ): %s (0x%lx)\n", ecatGetText(
                             dwRes), dwRes));
        }
        PERF_JOB_END(JOB_SendAllCycFrames);

        /* remove this code when using licensed version */
        if (EC_E_EVAL_EXPIRED == dwRes) {
            bRun = EC_FALSE; /* set shutdown flag */
        }

        /* Execute some administrative jobs. No bus traffic is performed by this function */
        PERF_JOB_START(JOB_MasterTimer);
        dwRes = ecatExecJob(eUsrJob_MasterTimer, EC_NULL);
        if (EC_E_NOERROR != dwRes && EC_E_INVALIDSTATE != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatExecJob(eUsrJob_MasterTimer, EC_NULL): %s (0x%lx)\n", ecatGetText(
                             dwRes), dwRes));
        }
        PERF_JOB_END(JOB_MasterTimer);

        /* send queued acyclic EtherCAT frames */
        PERF_JOB_START(JOB_SendAcycFrames);
        dwRes = ecatExecJob(eUsrJob_SendAcycFrames, EC_NULL);
        if (EC_E_NOERROR != dwRes && EC_E_INVALIDSTATE != dwRes && EC_E_LINK_DISCONNECTED != dwRes) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatExecJob(eUsrJob_SendAcycFrames, EC_NULL): %s (0x%lx)\n", ecatGetText(
                             dwRes), dwRes));
        }
        PERF_JOB_END(JOB_SendAcycFrames);

#if !(defined NO_OS)
    } while (!pEcThreadParam->bJobThreadShutdown);

    PERF_MEASURE_JOBS_SHOW();

    pEcThreadParam->bJobThreadRunning = EC_FALSE;
#else
    /* in case of NO_OS the job task function is called cyclically within the timer ISR */
} while (EC_FALSE);
pEcThreadParam->bJobThreadRunning = !pEcThreadParam->bJobThreadShutdown;
#endif

    Exit:
#if (defined EC_VERSION_RTEMS)
    rtems_task_delete(RTEMS_SELF);
#endif
    return;
}

/********************************************************************************/
/** \brief  Handler for master notifications
*
* \return  Status value.
*/
static EC_T_DWORD ecatNotifyCallback(
        EC_T_DWORD dwCode,       /**< [in]   Notification code */
        EC_T_NOTIFYPARMS *pParms /**< [in]   Notification parameters */
) {
    EC_T_DWORD dwRetVal = EC_E_NOERROR;
    T_EC_THREAD_PARAM *pEcThreadParam = EC_NULL;

    if ((EC_NULL == pParms) || (EC_NULL == pParms->pCallerData)) {
        dwRetVal = EC_E_INVALIDPARM;
        goto Exit;
    }

    pEcThreadParam = (T_EC_THREAD_PARAM *) (pParms->pCallerData);

    /* notification for application ? */
    if ((dwCode >= EC_NOTIFY_APP) && (dwCode <= EC_NOTIFY_APP + EC_NOTIFY_APP_MAX_CODE)) {
        /*****************************************************/
        /* Demo code: Remove/change this in your application */
        /* to get here the API ecatNotifyApp(dwCode, pParms) has to be called */
        /*****************************************************/
        dwRetVal = myAppNotify(dwCode - EC_NOTIFY_APP, pParms);
    } else {
        /* call the default handler */
        dwRetVal = pEcThreadParam->pNotInst->ecatNotify(dwCode, pParms);
    }

    Exit:
    return dwRetVal;
}

/********************************************************************************/
/** \brief  Handler for master RAS notifications
*
*
* \return  Status value.
*/
#ifdef ATEMRAS_SERVER

static EC_T_DWORD RasNotifyWrapper(
        EC_T_DWORD dwCode,
        EC_T_NOTIFYPARMS *pParms) {
    EC_T_DWORD dwRetVal = EC_E_NOERROR;
    CEmNotification *pNotInst = EC_NULL;

    if ((EC_NULL == pParms) || (EC_NULL == pParms->pCallerData)) {
        dwRetVal = EC_E_INVALIDPARM;
        goto Exit;
    }

    pNotInst = (CEmNotification *) (pParms->pCallerData);
    dwRetVal = pNotInst->emRasNotify(dwCode, pParms);
    Exit:
    return dwRetVal;
}

#endif

/*-MYAPP---------------------------------------------------------------------*/

/***************************************************************************************************/
/**
\brief  Initialize Application

\return EC_E_NOERROR on success, error code otherwise.
*/
static EC_T_DWORD myAppInit(const EC_T_PBYTE pbyConfig, T_EC_THREAD_PARAM *pEcThreadParam) {
    EC_UNREFPARM(pEcThreadParam);
    //    return EC_E_NOERROR;
    ////////===========My Own Code============/////////

    std::string configPath = (char *) pbyConfig;
    if (!pEcatConfig->parserYamlFile(configPath))
        goto Exit;

    if (!pEcatConfig->createSharedMemory())
        goto Exit;

//    pEcatConfig->ecatSlaveVec->resize(pEcatConfig->slave_number);
//    pEcatConfig->ecatSlaveNameVec->resize(pEcatConfig->slave_number, "");

    // Parse request state from command arguments --state
    if(strcasecmp(FLAGS_state.c_str(), "init") == 0)
        pEcatConfig->ecatInfo->ecatRequestState = EcatInfo::INIT;
    else if(strcasecmp(FLAGS_state.c_str(), "preop") == 0)
        pEcatConfig->ecatInfo->ecatRequestState = EcatInfo::PREOP;
    else if(strcasecmp(FLAGS_state.c_str(), "safeop") == 0)
        pEcatConfig->ecatInfo->ecatRequestState = EcatInfo::SAFEOP;
    else if(strcasecmp(FLAGS_state.c_str(), "op") == 0)
        pEcatConfig->ecatInfo->ecatRequestState = EcatInfo::OP;
    else
        pEcatConfig->ecatInfo->ecatRequestState = EcatInfo::UNKNOWN;

    return EC_E_NOERROR;

    Exit:
    return EC_E_ERROR;
}

/***************************************************************************************************/
/**
\brief  Initialize Slave Instance.

Find slave parameters.
\return EC_E_NOERROR on success, error code otherwise.
*/
static EC_T_DWORD myAppPrepare(T_EC_THREAD_PARAM *pEcThreadParam) {
    EC_T_WORD wFixedAddress = INVALID_FIXED_ADDR;
    EC_T_CFG_SLAVE_INFO oCfgSlaveListInfo;
    EC_T_DWORD dwRes = EC_E_NOERROR;
    T_EC_FLASH_DATA *pFlashData = &pEcThreadParam->FlashData;
    OsMemset(&oCfgSlaveListInfo, 0, sizeof(EC_T_CFG_SLAVE_INFO));

    if (pFlashData->nFlashAddress != 0xFFFF) {
        /* check if slave address is provided */
        if (pFlashData->nFlashAddress > 0) {
            wFixedAddress = (EC_T_WORD) pFlashData->nFlashAddress;

            /* now get the offset of this device in the process data buffer and some other infos */
            dwRes = ecatGetCfgSlaveInfo(EC_TRUE, wFixedAddress, &oCfgSlaveListInfo);
            if (dwRes != EC_E_NOERROR) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: ecatGetCfgSlaveInfo() returns with error=0x%x, slave address=%d\n", dwRes, wFixedAddress));
            } else {
                if (oCfgSlaveListInfo.dwPdSizeOut != 0) {
                    pFlashData->dwFlashPdOutBitSize = oCfgSlaveListInfo.dwPdSizeOut;
                    pFlashData->dwFlashPdOutBitOffs = oCfgSlaveListInfo.dwPdOffsOut;
                } else {
                    EcLogMsg(EC_LOG_LEVEL_ERROR,
                             (pEcLogContext, EC_LOG_LEVEL_ERROR, "Slave address=%d has no outputs, therefore flashing not possible\n", wFixedAddress));
                }
            }
        } else {
            pFlashData->dwFlashPdOutBitSize = S_oRegisterResults.dwPDOutSize * 8;
        }
        if (pFlashData->dwFlashPdOutBitSize > 0) {
            pFlashData->dwFlashInterval = 20000; /* flash every 20 msec */
            pFlashData->dwFlashBufSize = BIT2BYTE(pFlashData->dwFlashPdOutBitSize);
            pFlashData->pbyFlashBuf = (EC_T_BYTE *) OsMalloc(pFlashData->dwFlashBufSize);
            OsMemset(pFlashData->pbyFlashBuf, 0, pFlashData->dwFlashBufSize);
        }
    }

    //    return EC_E_NOERROR;

    ////////===========My Own Code============/////////

//    EC_T_CFG_SLAVE_INFO slaveInfo;
//    dwRes = ecatGetCfgSlaveInfo(EC_TRUE, 1001, &slaveInfo);
//
//    EC_T_WORD NumOfInpVar;
//    ecatGetSlaveInpVarInfoNumOf(EC_TRUE, 1001, &NumOfInpVar);
//    EC_T_WORD NumOfOutpVar;
//    ecatGetSlaveOutpVarInfoNumOf(EC_TRUE, 1001, &NumOfOutpVar);
//
//    std::cout << "Num of input var: " << NumOfInpVar << "; Num of output var: " << NumOfOutpVar
//              << "; Connected Slaves: " << ecatGetNumConnectedSlaves() << "; Configured Slaves: "
//              << ecatGetNumConfiguredSlaves() << "; CfgPdInSize: " << (int) slaveInfo.dwPdSizeIn << "; CfgPdOutSize: "
//              << (int) slaveInfo.dwPdSizeOut << std::endl;
//    std::cout << "PdInSize: " << (int) S_oRegisterResults.dwPDInSize << "; PdOutSize: "
//              << (int) S_oRegisterResults.dwPDOutSize << std::endl;
//
//    if (NumOfInpVar < 5 || NumOfOutpVar < 5) {
//        EcLogMsg(EC_LOG_LEVEL_ERROR,
//                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Please check out the configurations of PDO\n"));
//        goto Exit;
//    }

    if (ecatGetNumConfiguredSlaves() != pEcatConfig->slave_number) {
        EcLogMsg(EC_LOG_LEVEL_ERROR,
                 (pEcLogContext, EC_LOG_LEVEL_ERROR, "Slave number in ENI is not equal to the slave number in configurations file(eg. ecat_config.yaml) . "
                                                     "Please check out the configurations file(eg. ecat_config.yaml) and EcMaster ENI file\n"));
        goto Exit;
    }
//    else if (ecatGetNumConfiguredSlaves() > pEcatConfig->slave_number) {
//        EcLogMsg(EC_LOG_LEVEL_WARNING,
//                 (pEcLogContext, EC_LOG_LEVEL_WARNING,
//                         "Configured %d slaves, but only %d slaves\n", ecatGetNumConfiguredSlaves(), pEcatConfig->slave_number));
//    }


    return EC_E_NOERROR;

    Exit:
    return EC_E_NOTREADY;
}

/***************************************************************************************************/
/**
\brief  Setup slave parameters (normally done in PREOP state

  - SDO up- and Downloads
  - Read Object Dictionary

\return EC_E_NOERROR on success, error code otherwise.
*/
static EC_T_DWORD myAppSetup(T_EC_THREAD_PARAM *pEcThreadParam) {
    EC_UNREFPARM(pEcThreadParam);
//    return EC_E_NOERROR;


    ////============== MY OWN CODE =================////

    for (int i = 0; i < pEcatConfig->slave_number; ++i) {
        EC_T_CFG_SLAVE_INFO SlaveInfo;

        if (ecatGetCfgSlaveInfo(EC_TRUE, i + 1001, &SlaveInfo) != EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_ERROR,
                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatGetCfgSlaveInfo() Error!!\n"));
            goto Exit;
        }

        pEcatConfig->slaveCfg[i].name = SlaveInfo.abyDeviceName;
        EcLogMsg(EC_LOG_LEVEL_INFO,
                 (pEcLogContext, EC_LOG_LEVEL_INFO, "Slave %d is: %s\n", i, pEcatConfig->slaveCfg[i].name.c_str())); //("Slave " + i + " is: " + pEcatConfig->slaveCfg[i].name + "\n").c_str()
        pEcatConfig->ecatSlaveVec->at(i).slave_id = i;
        pEcatConfig->ecatSlaveNameVec->at(i) = SlaveInfo.abyDeviceName;


        EC_T_PROCESS_VAR_INFO VarInfo;

        ////=================Process Data Inputs==================////
        // 1. ec_status_word
        if (ecatFindInpVarByName(const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, STATUS_WORD).c_str()),
                                 &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindInpVarByName()]: Can not find input var: %s , ignored !!\n", pEcatConfig->getEcInpVarName(
                             i, STATUS_WORD).c_str()));

            pEcatConfig->slaveCfg[i].ecInpOffsets[STATUS_WORD] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.status_word) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Status Word Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecInpOffsets[STATUS_WORD] = VarInfo.nBitOffs;
        }



        // 2. ec_position_actual_value
        if (ecatFindInpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, POSITION_ACTUAL_VALUE).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindInpVarByName()]: Can not find input var: %s , ignored !!\n", pEcatConfig->getEcInpVarName(
                             i, POSITION_ACTUAL_VALUE).c_str()));

            pEcatConfig->slaveCfg[i].ecInpOffsets[POSITION_ACTUAL_VALUE] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.position_actual_value) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Position Actual Value Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecInpOffsets[POSITION_ACTUAL_VALUE] = VarInfo.nBitOffs;
        }


        //3. ec_velocity_actual_value
        if (ecatFindInpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, VELOCITY_ACTUAL_VALUE).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindInpVarByName()]: Can not find input var: %s , ignored !!\n", pEcatConfig->getEcInpVarName(
                             i, VELOCITY_ACTUAL_VALUE).c_str()));

            pEcatConfig->slaveCfg[i].ecInpOffsets[VELOCITY_ACTUAL_VALUE] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.velocity_actual_value) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Velocity Actual Value Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecInpOffsets[VELOCITY_ACTUAL_VALUE] = VarInfo.nBitOffs;
        }


        //4. ec_torque_actual_value
        if (ecatFindInpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, TORQUE_ACTUAL_VALUE).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindInpVarByName()]: Can not find input var: %s , ignored !!\n", pEcatConfig->getEcInpVarName(
                             i, TORQUE_ACTUAL_VALUE).c_str()));

            pEcatConfig->slaveCfg[i].ecInpOffsets[TORQUE_ACTUAL_VALUE] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.torque_actual_value) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Torque Actual Value Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecInpOffsets[TORQUE_ACTUAL_VALUE] = VarInfo.nBitOffs;
        }



        //5. ec_load_torque_value
        if (ecatFindInpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, LOAD_TORQUE_VALUE).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindInpVarByName()]: Can not find input var: %s , ignored !!\n", pEcatConfig->getEcInpVarName(
                             i, LOAD_TORQUE_VALUE).c_str()));

            pEcatConfig->slaveCfg[i].ecInpOffsets[LOAD_TORQUE_VALUE] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.load_torque_value) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Load Torque Value Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecInpOffsets[LOAD_TORQUE_VALUE] = VarInfo.nBitOffs;
        }

        //6. ec_secondary_position_value
        if (ecatFindInpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, SECONDARY_POSITION_VALUE).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindInpVarByName()]: Can not find input var: %s , ignored !!\n", pEcatConfig->getEcInpVarName(
                             i, SECONDARY_POSITION_VALUE).c_str()));

            pEcatConfig->slaveCfg[i].ecInpOffsets[SECONDARY_POSITION_VALUE] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.secondary_position_value) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Secondary Position Value Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecInpOffsets[SECONDARY_POSITION_VALUE] = VarInfo.nBitOffs;
        }

        //7. ec_secondary_velocity_value
        if (ecatFindInpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, SECONDARY_VELOCITY_VALUE).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindInpVarByName()]: Can not find input var: %s , ignored !!\n", pEcatConfig->getEcInpVarName(
                             i, SECONDARY_VELOCITY_VALUE).c_str()));

            pEcatConfig->slaveCfg[i].ecInpOffsets[SECONDARY_VELOCITY_VALUE] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.secondary_velocity_value) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Secondary Velocity Value Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecInpOffsets[SECONDARY_VELOCITY_VALUE] = VarInfo.nBitOffs;
        }



        ////=================Process Data Outputs==================////
        // 1. ec_mode_of_operation
        if (ecatFindOutpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, MODE_OF_OPERATION).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindOutpVarByName()]: Can not find output var: %s , ignored !!\n", pEcatConfig->getEcOutpVarName(
                             i, MODE_OF_OPERATION).c_str()));

            pEcatConfig->slaveCfg[i].ecOutpOffsets[MODE_OF_OPERATION] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.mode_of_operation) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Mode of Operation Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecOutpOffsets[MODE_OF_OPERATION] = VarInfo.nBitOffs;
        }


        // 2. ec_control_word
        if (ecatFindOutpVarByName(const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, CONTROL_WORD).c_str()),
                                  &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindOutpVarByName()]: Can not find output var: %s , ignored !!\n", pEcatConfig->getEcOutpVarName(
                             i, CONTROL_WORD).c_str()));

            pEcatConfig->slaveCfg[i].ecOutpOffsets[CONTROL_WORD] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.control_word) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Control Word Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecOutpOffsets[CONTROL_WORD] = VarInfo.nBitOffs;
        }



        // 3. ec_target_position
        if (ecatFindOutpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, TARGET_POSITION).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindOutpVarByName()]: Can not find output var: %s , ignored !!\n", pEcatConfig->getEcOutpVarName(
                             i, TARGET_POSITION).c_str()));

            pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_POSITION] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_position) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Target Position Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_POSITION] = VarInfo.nBitOffs;
        }


        // 4. ec_target_velocity
        if (ecatFindOutpVarByName(
                const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, TARGET_VELOCITY).c_str()),
                &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindOutpVarByName()]: Can not find output var: %s , ignored !!\n", pEcatConfig->getEcOutpVarName(
                             i, TARGET_VELOCITY).c_str()));

            pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_VELOCITY] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_velocity) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Target Velocity Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_VELOCITY] = VarInfo.nBitOffs;
        }


        // 5. ec_target_torque
        if (ecatFindOutpVarByName(const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, TARGET_TORQUE).c_str()),
                                  &VarInfo) !=
            EC_E_NOERROR) {
            EcLogMsg(EC_LOG_LEVEL_WARNING,
                     (pEcLogContext, EC_LOG_LEVEL_WARNING, "[ecatFindOutpVarByName()]: Can not find output var: %s , ignored !!\n", pEcatConfig->getEcOutpVarName(
                             i, TARGET_TORQUE).c_str()));

            pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_TORQUE] = INT_MAX;
        } else {
            if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_torque) * 8 != VarInfo.nBitSize) {
                EcLogMsg(EC_LOG_LEVEL_ERROR,
                         (pEcLogContext, EC_LOG_LEVEL_ERROR, "Target Torque Bit Size Error!!\n"));
                goto Exit;
            }
            pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_TORQUE] = VarInfo.nBitOffs;
        }

    }

    ecatPerfMeasReset(&pEcThreadParam->TscMeasDesc, 0xFFFFFFFF); /* clear job times of startup phase */

    return EC_E_NOERROR;

    Exit:
    return EC_E_ERROR;


}


/***************************************************************************************************/
/**
\brief  demo application ready working process data function.

  This function is called in SAFEOP.

*/
static EC_T_DWORD myAppReadypd(T_EC_THREAD_PARAM *pEcThreadParam,
                               EC_T_BYTE *pbyPDIn,
                               EC_T_BYTE *pbyPDOut
) {
    EC_UNREFPARM(pEcThreadParam);
    EC_UNREFPARM(pbyPDIn);
    EC_UNREFPARM(pbyPDIn);

/*
//    for (int i = 0; i < pEcatConfig->slave_number; ++i) {
//        EC_T_CFG_SLAVE_INFO SlaveInfo;
//
//        if (ecatGetCfgSlaveInfo(EC_TRUE, i + 1001, &SlaveInfo) != EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatGetCfgSlaveInfo() Error!!\n"));
//            goto Exit;
//        }
//
//        pEcatConfig->slaveCfg[i].name = SlaveInfo.abyDeviceName;
//        EcLogMsg(EC_LOG_LEVEL_INFO,
//                 (pEcLogContext, EC_LOG_LEVEL_INFO, (pEcatConfig->slaveCfg[i].name + "\n").c_str()));
//        pEcatConfig->ecatSlaveVec->at(i).slave_id = i;
//        pEcatConfig->ecatSlaveNameVec->at(i) = SlaveInfo.abyDeviceName;
//
//
//        EC_T_PROCESS_VAR_INFO VarInfo;
//
//        ////=================Process Data Inputs==================////
//        // 1. ec_status_word
//        if (ecatFindInpVarByName(const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, STATUS_WORD).c_str()),
//                                 &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindInpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.status_word) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Status Word Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecInpOffsets[STATUS_WORD] = VarInfo.nBitOffs;
//
//
//        // 2. ec_position_actual_value
//        if (ecatFindInpVarByName(
//                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, POSITION_ACTUAL_VALUE).c_str()),
//                &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindInpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.position_actual_value) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Position Actual Value Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecInpOffsets[POSITION_ACTUAL_VALUE] = VarInfo.nBitOffs;
//
//        //3. ec_velocity_actual_value
//        if (ecatFindInpVarByName(
//                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, VELOCITY_ACTUAL_VALUE).c_str()),
//                &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindInpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.velocity_actual_value) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Velocity Actual Value Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecInpOffsets[VELOCITY_ACTUAL_VALUE] = VarInfo.nBitOffs;
//
//
//        //4. ec_torque_actual_value
//        if (ecatFindInpVarByName(
//                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, TORQUE_ACTUAL_VALUE).c_str()),
//                &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindInpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.torque_actual_value) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Torque Actual Value Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecInpOffsets[TORQUE_ACTUAL_VALUE] = VarInfo.nBitOffs;
//
//
//        //5. ec_load_torque_value
//        if (ecatFindInpVarByName(
//                const_cast<EC_T_CHAR *>(pEcatConfig->getEcInpVarName(i, LOAD_TORQUE_VALUE).c_str()),
//                &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindInpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.load_torque_value) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Load Torque Value Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecInpOffsets[LOAD_TORQUE_VALUE] = VarInfo.nBitOffs;
//
//
//        ////=================Process Data Outputs==================////
//        // 1. ec_mode_of_operation
//        if (ecatFindOutpVarByName(
//                const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, MODE_OF_OPERATION).c_str()),
//                &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindOutpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.mode_of_operation) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Mode of Operation Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecOutpOffsets[MODE_OF_OPERATION] = VarInfo.nBitOffs;
//
//
//        // 2. ec_control_word
//        if (ecatFindOutpVarByName(const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, CONTROL_WORD).c_str()),
//                                  &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindOutpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.control_word) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Control Word Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecOutpOffsets[CONTROL_WORD] = VarInfo.nBitOffs;
//
//
//        // 3. ec_target_position
//        if (ecatFindOutpVarByName(
//                const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, TARGET_POSITION).c_str()),
//                &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindOutpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_position) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Target Position Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_POSITION] = VarInfo.nBitOffs;
//
//
//        // 4. ec_target_velocity
//        if (ecatFindOutpVarByName(
//                const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, TARGET_VELOCITY).c_str()),
//                &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindOutpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_velocity) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Target Velocity Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_VELOCITY] = VarInfo.nBitOffs;
//
//
//        // 5. ec_target_torque
//        if (ecatFindOutpVarByName(const_cast<EC_T_CHAR *>(pEcatConfig->getEcOutpVarName(i, TARGET_TORQUE).c_str()),
//                                  &VarInfo) !=
//            EC_E_NOERROR) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "ecatFindOutpVarByName() Error!!\n"));
//            goto Exit;
//        }
//        if (sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_torque) * 8 != VarInfo.nBitSize) {
//            EcLogMsg(EC_LOG_LEVEL_ERROR,
//                     (pEcLogContext, EC_LOG_LEVEL_ERROR, "Target Torque Bit Size Error!!\n"));
//            goto Exit;
//        }
//        pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_TORQUE] = VarInfo.nBitOffs;
//    }
//
*/
    return EC_E_NOERROR;

    Exit:
    return EC_E_ERROR;
}



/***************************************************************************************************/
/**
\brief  demo application working process data function.

  This function is called in every cycle after the the master stack is started.

*/
static EC_T_DWORD myAppWorkpd(T_EC_THREAD_PARAM *pEcThreadParam,
                              EC_T_BYTE *pbyPDIn, /* [in]  pointer to process data input buffer */
                              EC_T_BYTE *pbyPDOut /* [in]  pointer to process data output buffer */
) {
    ////============== Origin =================////
    //    EC_UNREFPARM(pbyPDIn);
    //    T_EC_FLASH_DATA* pFlashData = &pEcThreadParam->FlashData;
    //
    //    /* demo code flashing */
    //    if (pFlashData->dwFlashPdOutBitSize != 0)
    //    {
    //        pFlashData->dwFlashTimer += pEcThreadParam->dwBusCycleTimeUsec;
    //        if (pFlashData->dwFlashTimer >= pFlashData->dwFlashInterval)
    //        {
    //            pFlashData->dwFlashTimer = 0;
    //
    //            /* flash with pattern */
    //            pFlashData->byFlashVal++;
    //            OsMemset(pFlashData->pbyFlashBuf, pFlashData->byFlashVal, pFlashData->dwFlashBufSize);
    //
    //            /* update PdOut */
    //            EC_COPYBITS(pbyPDOut, pFlashData->dwFlashPdOutBitOffs, pFlashData->pbyFlashBuf, 0, pFlashData->dwFlashPdOutBitSize);
    //        }
    //    }

    ////============== shared memory =================////
    pEcatConfig->ecatInfo->timestamp = boost::chrono::system_clock::now();

    for (int i = 0; i < pEcatConfig->slave_number; ++i) {

        ////=================Process Data Inputs==================////
        // 1. ec_status_word
        if (pEcatConfig->slaveCfg[i].ecInpOffsets[STATUS_WORD] != INT_MAX) {
            EC_GETBITS(pbyPDIn, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).inputs.status_word)),
                       pEcatConfig->slaveCfg[i].ecInpOffsets[STATUS_WORD],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.status_word) * 8);
        }

        // 2. ec_position_actual_value
        if (pEcatConfig->slaveCfg[i].ecInpOffsets[POSITION_ACTUAL_VALUE] != INT_MAX) {
            EC_GETBITS(pbyPDIn, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).inputs.position_actual_value)),
                       pEcatConfig->slaveCfg[i].ecInpOffsets[POSITION_ACTUAL_VALUE],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.position_actual_value) * 8);
        }

        //3. ec_velocity_actual_value
        if (pEcatConfig->slaveCfg[i].ecInpOffsets[VELOCITY_ACTUAL_VALUE] != INT_MAX) {
            EC_GETBITS(pbyPDIn, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).inputs.velocity_actual_value)),
                       pEcatConfig->slaveCfg[i].ecInpOffsets[VELOCITY_ACTUAL_VALUE],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.velocity_actual_value) * 8);
        }

        //4. ec_torque_actual_value
        if (pEcatConfig->slaveCfg[i].ecInpOffsets[TORQUE_ACTUAL_VALUE] != INT_MAX) {
            EC_GETBITS(pbyPDIn, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).inputs.torque_actual_value)),
                       pEcatConfig->slaveCfg[i].ecInpOffsets[TORQUE_ACTUAL_VALUE],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.torque_actual_value) * 8);
        }

        //5. ec_load_torque_value
        if (pEcatConfig->slaveCfg[i].ecInpOffsets[LOAD_TORQUE_VALUE] != INT_MAX) {
            EC_GETBITS(pbyPDIn, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).inputs.load_torque_value)),
                       pEcatConfig->slaveCfg[i].ecInpOffsets[LOAD_TORQUE_VALUE],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.load_torque_value) * 8);
        }

        //6. ec_secondary_position_value
        if (pEcatConfig->slaveCfg[i].ecInpOffsets[SECONDARY_POSITION_VALUE] != INT_MAX) {
            EC_GETBITS(pbyPDIn, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).inputs.secondary_position_value)),
                       pEcatConfig->slaveCfg[i].ecInpOffsets[SECONDARY_POSITION_VALUE],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.secondary_position_value) * 8);
        }

        //7. ec_secondary_velocity_value
        if (pEcatConfig->slaveCfg[i].ecInpOffsets[SECONDARY_VELOCITY_VALUE] != INT_MAX) {
            EC_GETBITS(pbyPDIn, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).inputs.secondary_velocity_value)),
                       pEcatConfig->slaveCfg[i].ecInpOffsets[SECONDARY_VELOCITY_VALUE],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).inputs.secondary_velocity_value) * 8);
        }

        ////=================Process Data Outputs==================////
        // 1. ec_mode_of_operation
        if (pEcatConfig->slaveCfg[i].ecOutpOffsets[MODE_OF_OPERATION] != INT_MAX) {
            EC_SETBITS(pbyPDOut, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).outputs.mode_of_operation)),
                       pEcatConfig->slaveCfg[i].ecOutpOffsets[MODE_OF_OPERATION],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.mode_of_operation) * 8);
        }

        // 2. ec_control_word
        if (pEcatConfig->slaveCfg[i].ecOutpOffsets[CONTROL_WORD] != INT_MAX) {
            EC_SETBITS(pbyPDOut, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).outputs.control_word)),
                       pEcatConfig->slaveCfg[i].ecOutpOffsets[CONTROL_WORD],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.control_word) * 8);
        }

        // 3. ec_target_position
        if (pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_POSITION] != INT_MAX) {
            EC_SETBITS(pbyPDOut, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).outputs.target_position)),
                       pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_POSITION],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_position) * 8);
        }

        // 4. ec_target_velocity
        if (pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_VELOCITY] != INT_MAX) {
            EC_SETBITS(pbyPDOut, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).outputs.target_velocity)),
                       pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_VELOCITY],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_velocity) * 8);
        }

        // 5. ec_target_torque
        if (pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_TORQUE] != INT_MAX) {
            EC_SETBITS(pbyPDOut, (EC_T_BYTE *) (&(pEcatConfig->ecatSlaveVec->at(i).outputs.target_torque)),
                       pEcatConfig->slaveCfg[i].ecOutpOffsets[TARGET_TORQUE],
                       sizeof(pEcatConfig->ecatSlaveVec->at(i).outputs.target_torque) * 8);
        }

    }

    for (auto &sem: pEcatConfig->sem_mutex) {
        int val = 0;
        sem_getvalue(sem, &val);
        if (val < 1)
            sem_post(sem);
    }


    return EC_E_NOERROR;

    Exit:

    for (auto &sem: pEcatConfig->sem_mutex) {
        sem_post(sem);
    }


    return EC_E_ERROR;
}

/***************************************************************************************************/
/**
\brief  demo application doing some diagnostic tasks

  This function is called in sometimes from the main demo task
*/
static EC_T_DWORD myAppDiagnosis(T_EC_THREAD_PARAM *pEcThreadParam) {
    EC_UNREFPARM(pEcThreadParam);

    return EC_E_NOERROR;
}

/********************************************************************************/
/** \brief  Handler for application notifications
*
*  !!! No blocking API shall be called within this function!!!
*  !!! Function is called by cylic task                    !!!
*
* \return  Status value.
*/
static EC_T_DWORD myAppNotify(
        EC_T_DWORD dwCode,       /* [in]  Application notification code */
        EC_T_NOTIFYPARMS *pParms /* [in]  Notification parameters */
) {
    EC_T_DWORD dwRetVal = EC_E_ERROR;
#if (defined INCLUDE_LOG_MESSAGES)
    T_EC_THREAD_PARAM *pEcThreadParam = (T_EC_THREAD_PARAM *) (pParms->pCallerData);
#else
    EC_UNREFPARM(pParms);
#endif

    /* dispatch notification code */
    switch (dwCode) {
        case 1:
            EcLogMsg(EC_LOG_LEVEL_INFO,
                     (pEcLogContext, EC_LOG_LEVEL_INFO, "Application notification code=%d received\n", dwCode));
            /* dwRetVal = EC_E_NOERROR; */
            break;
        case 2:
            break;
        default:
            break;
    }

    return dwRetVal;
}

/*-END OF SOURCE FILE--------------------------------------------------------*/
