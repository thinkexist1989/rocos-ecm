/*-----------------------------------------------------------------------------
 * ecatProcessCommon.h
 * Copyright                acontis technologies GmbH, Weingarten, Germany
 * Response                 Stefan Zintgraf
 * Description              header for demo common parts
 *---------------------------------------------------------------------------*/

#ifndef INC_ECATDEMOCOMMON_H
#define INC_ECATDEMOCOMMON_H 1

/*-LOGGING-------------------------------------------------------------------*/
#ifndef pEcLogParms
#define pEcLogParms (&(pEcThreadParam->LogParms))
#endif

/*-INCLUDES------------------------------------------------------------------*/
#ifndef INC_LOGGING
#include "Logging.h"
#endif
#ifndef INC_ECINTERFACECOMMON
#include "EcInterfaceCommon.h"
#endif
#ifndef INC_SLAVEINFO
#include "SlaveInfo.h"
#endif
#ifndef INC_ECATNOTIFICATION
#include "ecatNotification.h"
#endif

/*-DEFINES-------------------------------------------------------------------*/
#define ETHERCAT_STATE_CHANGE_TIMEOUT   15000   /* master state change timeout in ms */
#define ETHERCAT_SCANBUS_TIMEOUT        10000   /* scanbus timeout in ms, see also EC_SB_DEFAULTTIMEOUT */

#define ETHERCAT_DC_TIMEOUT             12000   /* DC initialization timeout in ms */
#define ETHERCAT_DC_ARMW_BURSTCYCLES    10000   /* DC burst cycles (static drift compensation) */
#define ETHERCAT_DC_ARMW_BURSTSPP       12      /* DC burst bulk (static drift compensation) */
#define ETHERCAT_DC_DEV_LIMIT           13      /* DC deviation limit (highest bit tolerate by the broadcast read) */
#define ETHERCAT_DC_SETTLE_TIME         1500    /* DC settle time in ms */

#define APP_ERROR         -1
#define SYNTAX_ERROR      -2
#define APP_OUT_OF_MEMORY -3
#define APP_NOERROR        0

/* tag names for DemoConfig.xml file */
#define DEMO_CFG_DEFAULT_FILENAME       (EC_T_CHAR*)"DemoConfig.xml"
#define DEMO_CFG_TAG_ENI_FILENAME       (EC_T_CHAR*)"Config\\Common\\ENIFileName"
#define DEMO_CFG_TAG_LOG_FILEPREFIX     (EC_T_CHAR*)"Config\\Common\\LogFilePrefix"
#define DEMO_CFG_TAG_LINK_LAYER         (EC_T_CHAR*)"Config\\Common\\LinkLayer"
#define DEMO_CFG_TAG_LINK_LAYER2        (EC_T_CHAR*)"Config\\Common\\LinkLayer2"
#define DEMO_CFG_TAG_DURATION           (EC_T_CHAR*)"Config\\Common\\DemoDuration"
#define DEMO_CFG_TAG_CPU_AFFINITY       (EC_T_CHAR*)"Config\\Common\\CpuAffinity"
#define DEMO_CFG_TAG_VERBOSITY_LVL      (EC_T_CHAR*)"Config\\Common\\VerbosityLevel"
#define DEMO_CFG_TAG_PERF_MEASURE       (EC_T_CHAR*)"Config\\Common\\PerfMeasurement"
#define DEMO_CFG_TAG_RAS_ENABLED        (EC_T_CHAR*)"Config\\Common\\RASEnabled"
#define DEMO_CFG_TAG_RAS_PORT           (EC_T_CHAR*)"Config\\Common\\RASPort"
#define DEMO_CFG_TAG_AUXCLK             (EC_T_CHAR*)"Config\\Common\\AuxClk"
#define DEMO_CFG_TAG_BUSCYCLETIME       (EC_T_CHAR*)"Config\\Common\\BusCycleTime"

/*-FORWARD DECLARATIONS------------------------------------------------------*/
struct _EC_T_MBXTFER;

/*-TYPEDEFS------------------------------------------------------------------*/
typedef struct _T_EC_FLASH_DATA
{
    EC_T_INT   nFlashAddress;
    EC_T_DWORD dwFlashPdOutBitSize; /* Size of process data output memory */
    EC_T_DWORD dwFlashPdOutBitOffs; /* Process data offset of output data */
    EC_T_DWORD dwFlashTimer;
    EC_T_DWORD dwFlashInterval;
    EC_T_BYTE  byFlashVal;          /* flash pattern */
    EC_T_BYTE* pbyFlashBuf;         /* flash buffer */
    EC_T_DWORD dwFlashBufSize;      /* flash buffer */
} T_EC_FLASH_DATA;
typedef struct _T_EC_THREAD_PARAM
{
    EC_T_DWORD                dwMasterID;           /**< [in] Master instance */
    CEcTimer                  oDuration;            /**<      Demo run time */
    EC_T_BOOL                 bJobThreadRunning;    /**< [in] Job Thread is running */
    EC_T_BOOL                 bJobThreadShutdown;   /**< [in] Job Thread Shutdown requested */
    EC_T_VOID*                pvTimingEvent;        /**< [in] Event create for timing purposes */
    EC_T_VOID*                pvTtsEvent;           /**< [in] Event create for timing purposes */
    EC_T_DWORD                dwCpuIndex;           /**< [in] SMP systems: CPU index */
    EC_T_LOG_PARMS            LogParms;             /**< [in] logging context, function, level */
    EC_T_INT                  nVerbose;             /**< [in] verbosity level */
    CEmNotification*          pNotInst;             /**< [in] Notification class pointer */
    EC_T_VOID*                pvApplSpecParm1;      /**< [in] application specific parameters 1 */
    EC_T_VOID*                pvApplSpecParm2;      /**< [in] application specific parameters 2 */
    EC_T_DWORD                dwDcmCtlCycleDivider; /**< [in] DCM controller cycle divider*/
    EC_T_BOOL                 bDcmCtlInitialized;   /**< [in] EC_TRUE if DCM controller is initialized */
    EC_T_DWORD                dwBusCycleTimeUsec;   /**< [in] bus cycle time in usec */
    EC_T_TSC_MEAS_DESC        TscMeasDesc;
    T_EC_FLASH_DATA         FlashData;
    EC_T_BYTE                 byDigInputLastVal;
} T_EC_THREAD_PARAM;

/*-GLOBAL VARIABLES-----------------------------------------------------------*/
extern volatile EC_T_BOOL  bRun; // volatile修饰符的作用是防止编译器优化，直接从内存中读取变量的值，而不是使用寄存器中的备份（多线程更新）

/*-FUNCTION DECLARATION------------------------------------------------------*/
EC_T_DWORD CoeReadObjectDictionary(
        T_EC_THREAD_PARAM* pEcThreadParam
   , EC_T_BOOL*           pbStopReading   /**< [in]   Pointer to "Stop Reading" flag */
   , EC_T_DWORD           dwNodeId        /**< [in]   Slave Id to query ODL from  */
   , EC_T_BOOL            bPerformUpload  /**< [in]   EC_TRUE: do SDO Upload */
   , EC_T_DWORD           dwTimeout       /**< [in]   Individual call time-out */
   );
EC_T_VOID LinkDbgMsg(
        T_EC_THREAD_PARAM* pEcThreadParam
   , EC_T_BYTE            byEthTypeByte0
   , EC_T_BYTE            byEthTypeByte1
   , EC_T_CHAR*           szMsg
   );
EC_T_DWORD SetFrameLoss(
        T_EC_THREAD_PARAM* pEcThreadParam
   , EC_T_DWORD           dwNumGoodFramesAfterStart
   , EC_T_DWORD           dwLikelihoodPpm
   , EC_T_DWORD           dwFixedLossNumGoodFrms
   , EC_T_DWORD           dwFixedLossNumBadFrms
   , EC_T_BOOL            bSuppressCyclicMessage
   );
EC_T_BOOL PrintSlaveInfos(T_EC_THREAD_PARAM* pEcThreadParam);
EC_T_VOID PrintCfgSlavesInfo(T_EC_THREAD_PARAM* pEcThreadParam);
EC_T_VOID PrintBusSlavesInfo(T_EC_THREAD_PARAM* pEcThreadParam);
EC_T_BOOL FindSlaveGetFixedAddr(
        T_EC_THREAD_PARAM* pEcThreadParam
    , EC_T_DWORD          dwSlaveInstance       /**< [in]   Slave instance (0 = first matching, 1 = second, ...) */
    , EC_T_DWORD          dwVendorId            /**< [in]   Vendor Id of slave to search */
    , EC_T_DWORD          dwProductCode         /**< [in]   Product Code of slave to search */
    , EC_T_WORD*          pwPhysAddr            /**< [out]  Station address of slave */
    );
EC_T_BOOL FindCfgSlaveGetFixedAddr(
        T_EC_THREAD_PARAM* pEcThreadParam
    , EC_T_DWORD          dwSlaveInstance       /**< [in]   Slave instance (0 = first matching, 1 = second, ...) */
    , EC_T_DWORD          dwVendorId            /**< [in]   Vendor Id of slave to search */
    , EC_T_DWORD          dwProductCode         /**< [in]   Product Code of slave to search */
    , EC_T_WORD*          pwPhysAddr            /**< [out]  Station address of slave */
    );
EC_T_CHAR* SlaveVendorText(T_eEtherCAT_Vendor EV);
EC_T_CHAR* SlaveProdCodeText(T_eEtherCAT_Vendor EV, T_eEtherCAT_ProductCode EPC);
EC_T_CHAR* ESCTypeText(EC_T_BYTE byESCType);
EC_T_VOID  HandleMbxTferReqError(
        T_EC_THREAD_PARAM*  pEcThreadParam
   , EC_T_CHAR*            szErrMsg
   , EC_T_DWORD            dwErrorCode
   , EC_T_MBXTFER*         pMbxTfer
   );

#endif /* INC_ECATPROCESSCOMMON_H */

/*-END OF SOURCE FILE--------------------------------------------------------*/
