/*-----------------------------------------------------------------------------
 * ecatProcess.h
 * Copyright                acontis technologies GmbH, Weingarten, Germany
 * Response                 Stefan Zintgraf
 * Description              EtherCAT Master Process header
 * Modified                 Yang Luo , luoyang@sia.cn
 *---------------------------------------------------------------------------*/

/*-INCLUDES------------------------------------------------------------------*/
#include "AtEthercat.h"
#include "ecatProcessConfig.h"
#include "ecatProcessCommon.h"

#ifdef VXWORKS
#include "wvLib.h"
#endif

/*-DEFINES-------------------------------------------------------------------*/
#define MAX_LINKLAYER 5

/* the RAS server is necessary to support the EC-Engineer or other remote applications */
#if (!defined ATEMRAS_SERVER) && (defined EC_SOCKET_SUPPORTED)
#define ATEMRAS_SERVER
#endif

#define REMOTE_WD_TO_LIMIT          10000
#define REMOTE_CYCLE_TIME           2

/*-FUNCTION DECLARATIONS-----------------------------------------------------*/
EC_T_DWORD ecatProcess(
     EC_T_PBYTE          pbyCnf
    ,EC_T_CNF_TYPE       eCnfType
    ,EC_T_PBYTE          pbyEni
    ,EC_T_DWORD          dwCnfDataLen
    ,EC_T_DWORD          dwBusCycleTimeUsec
    ,EC_T_INT            nVerbose
    ,EC_T_DWORD          dwDuration
    ,EC_T_LINK_PARMS*    poLinkParms
    ,EC_T_VOID*          pvTimingEvent
#if (defined INCLUDE_TTS)
   ,EC_T_VOID*           pvTtsEvent
#endif
    ,EC_T_DWORD          dwCpuIndex
    ,EC_T_BOOL           bEnaPerfJobs
    ,EC_T_INT            nFlashAddress
#if (defined ATEMRAS_SERVER)
    ,EC_T_WORD           wServerPort
#endif
#if (defined VLAN_FRAME_SUPPORT)
    ,EC_T_BOOL           bVLANEnable
    ,EC_T_WORD           wVLANId
    ,EC_T_BYTE           byVLANPrio
#endif
    ,EC_T_LINK_PARMS*    poLinkParmsRed
    ,EC_T_DCM_MODE       eDcmMode
    ,EC_T_BOOL           bCtlOff
    ,EC_T_OS_PARMS*      poOsParms
    );

/*--------------------------------------------------------------------------*/
/* Performance measurements of jobs                                         */
/* This is only available on CPUs with TSC support                          */
/*--------------------------------------------------------------------------*/
#define JOB_ProcessAllRxFrames  0
#define JOB_SendAllCycFrames    1
#define JOB_MasterTimer         2
#define JOB_SendAcycFrames      3
#define PERF_CycleTime          4
#define PERF_myAppWorkpd        5
#define PERF_DCM_Logfile        6
#define MAX_JOB_NUM             7

#define PERF_MEASURE_JOBS_INIT(msgcb)   ecatPerfMeasInit(&pEcThreadParam->TscMeasDesc,0,MAX_JOB_NUM,msgcb);ecatPerfMeasEnable(&pEcThreadParam->TscMeasDesc)
#define PERF_MEASURE_JOBS_DEINIT()      ecatPerfMeasDeinit(&pEcThreadParam->TscMeasDesc)
#define PERF_MEASURE_JOBS_SHOW()        ecatPerfMeasShow(&pEcThreadParam->TscMeasDesc,0xFFFFFFFF,S_aszMeasInfo)
#define PERF_JOB_START(nJobIndex)       ecatPerfMeasStart(&pEcThreadParam->TscMeasDesc,(EC_T_DWORD)(nJobIndex))
#define PERF_JOB_END(nJobIndex)         ecatPerfMeasEnd(&pEcThreadParam->TscMeasDesc,(EC_T_DWORD)(nJobIndex))

/*-END OF SOURCE FILE--------------------------------------------------------*/
