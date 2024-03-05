/*----------------------------------------------------------------------------
 * EcOs.cpp
 * Copyright                acontis technologies GmbH, Weingarten, Germany
 * Response                 Paul Bussmann
 * Description              EC-Master OS-Layer for Linux
 *---------------------------------------------------------------------------*/

#define EC_OS_CPP

/*-LOGGING-------------------------------------------------------------------*/
extern "C" struct _EC_T_LOG_PARMS* G_pLogParmsEcOs;
#define pEcLogParms G_pLogParmsEcOs

/*-INCLUDES------------------------------------------------------------------*/
#include <errno.h> /* errno */
#include <limits.h> /* PTHREAD_STACK_MIN */
#include <syslog.h> /* syslog */
#include <dlfcn.h> /* dynamic loader */
#include <sys/prctl.h> /* prctl() */

#include <EcOs.h>
#include <EcError.h>
#include <EcLog.h>

/*-DEFINES--------------------------------------------------------------------*/
#define TRACE_FILENAME "ecatTrace.log"

#ifndef PAGE_SIZE
#   define PAGE_SIZE 0x1000
#endif
#ifndef PAGE_UP
#   define PAGE_UP(addr)   (((addr)+((PAGE_SIZE)-1))&(~((PAGE_SIZE)-1)))
#endif
#ifndef PAGE_DOWN
#   define PAGE_DOWN(addr) ((addr)&(~((PAGE_SIZE)-1)))
#endif

#define NSEC_PER_SEC                (1000000000)
#define TIMER_THREAD_PRIO           ((EC_T_DWORD)99)   /* EtherCAT master timer task (tEcTimingTask) */
#define LOG_THREAD_STACKSIZE        0x4000

/*-LOGGING-------------------------------------------------------------------*/
EC_T_DWORD LogMsgOsPrintf(struct _EC_T_LOG_CONTEXT* /* pContext */, EC_T_DWORD /* dwLogMsgSeverity*/, const EC_T_CHAR* szFormat, ...)
{
    EC_T_VALIST vaArgs;
    EC_VASTART(vaArgs, szFormat);
    OsVprintf(szFormat, vaArgs);
    EC_VAEND(vaArgs);
    return EC_E_NOERROR;
}

static struct _EC_T_LOG_PARMS S_oLogParmsEcOs = {EC_LOG_LEVEL_ERROR, LogMsgOsPrintf, EC_NULL};
struct _EC_T_LOG_PARMS* G_pLogParmsEcOs = &S_oLogParmsEcOs;

/*-TYPEDEFS------------------------------------------------------------------*/
typedef struct _EC_T_AUXCLK_DESC
{
    EC_T_DWORD          dwCpuIndex;         /* SMP systems: CPU index */

    EC_T_VOID*          pvTimingEvent;      /* event handle */
    EC_T_INT            nCycleTimeNsec;     /* cycle time in nsec, current */
    EC_T_INT            nOrigCycleTimeNsec; /* cycle time in nsec, original */
    EC_T_BOOL           bShutdown;          /* EC_TRUE if aux thread shall shut down */
    EC_T_BOOL           bIsRunning;         /* EC_TRUE if the aux thread is running */
} EC_T_AUXCLK_DESC;
static EC_T_AUXCLK_DESC S_AuxDesc;

/*-LOCALS--------------------------------------------------------------------*/
static EC_PF_SYSTIME    S_pfSystemTimeGet       = EC_NULL;
static EC_PF_GETLINKLAYERREGFUNC S_pfGetLinkLayerRegFunc = EC_NULL;

#ifdef DEBUG
static EC_T_BOOL        S_bSpinLockActive       = EC_FALSE;     /* flag: if set, no OS calls are allowed */
static pthread_t        S_SpinLockThread        = EC_NULL;      /* thread which owns the spin lock */
#endif
static pthread_t        S_JobTaskThread         = EC_NULL;      /* job thread */
#ifdef DEBUGTRACE
static FILE* S_pfTraceFile = EC_NULL;
#endif

#if (defined INCLUDE_OSPERF)
static EC_T_UINT64      S_qwLastPerfTime    = 0;
static EC_T_BOOL        S_bPerfTimerActive  = EC_FALSE;
#endif

static E_SLEEP S_ESleep = eSLEEP_CLOCK_NANOSLEEP;
#define MAX_SODIR_LEN 256

struct ThreadArg
{
#define MAX_THREADNAME_LEN 16

   EC_PF_THREADENTRY entry;
   void*             arg;
   char              name[MAX_THREADNAME_LEN];
   uint              cpuMask;
   uint              threadPrio;
};

#if (defined DEBUG)
static unsigned CpuSetToMask(cpu_set_t cpuSet)
{
   unsigned cpuMask = 0;
   int cpuCnt = CPU_COUNT(&cpuSet);
   for (int i = 0; i < cpuCnt; ++i)
   {
     cpuMask |= (1 << i);
   }
   return cpuMask;
}
#endif /* DEBUG */

static cpu_set_t CpuMaskToSet(unsigned cpuMask)
{
   cpu_set_t cpuSet;
   CPU_ZERO(&cpuSet);
   for (int i = 0; i < 32; ++i)
   {
     if (cpuMask & (1 << i))
     {
        CPU_SET(i, &cpuSet);
     }
   }
   return cpuSet;
}

/******************************************************************************/
/** \brief OS Layer initialization.
*
* \return status code
*/
EC_API EC_T_DWORD EC_API_FNCALL OsInit(EC_T_OS_PARMS* pOsParms)
{
    EC_T_DWORD dwRetVal = EC_E_ERROR;
    
    if (EC_NULL != pOsParms)
    {
        if (EC_OS_PARMS_SIGNATURE != pOsParms->dwSignature)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: OsInit() OS layer parameter signature 0x%08X wrong! Expected EC_OS_PARMS_SIGNATURE (0x%08X).\n",
                pOsParms->dwSignature, EC_OS_PARMS_SIGNATURE));
            dwRetVal = EC_E_INVALIDPARM;
            goto Exit;
        }
        if (pOsParms->dwSize < sizeof(EC_T_OS_PARMS))
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: OsInit() OS layer parameter size %d too small! Size of EC_T_OS_PARMS is %d bytes.\n",
                pOsParms->dwSize, sizeof(EC_T_OS_PARMS)));
            dwRetVal = EC_E_INVALIDPARM;
            goto Exit;
        }
        if (EC_NULL != pOsParms->pLogParms)
        {
            OsMemcpy(G_pLogParmsEcOs, pOsParms->pLogParms, sizeof(EC_T_LOG_PARMS));
        }
        if ((EC_NULL == S_pfSystemTimeGet) && (EC_NULL != pOsParms->pfSystemTimeGet))
        {
            S_pfSystemTimeGet = pOsParms->pfSystemTimeGet;
        }
    }

    dwRetVal = EC_E_NOERROR;
Exit:
    return dwRetVal;
}

/******************************************************************************/
/** \brief OS Layer de-initialization.
*
* \return status code
*/
EC_API EC_T_DWORD EC_API_FNCALL OsDeinit(EC_T_VOID)
{
#ifdef DEBUGTRACE
    if( S_pfTraceFile != EC_NULL ) fclose( S_pfTraceFile );
    S_pfTraceFile = EC_NULL;
#endif
    return EC_E_NOERROR;
}

/******************************************************************************/
/** \brief Determine millisecond counter value since start.
*
* \return Milliseconds since start (start = first call to this function).
*/
EC_T_DWORD OsQueryMsecCount(EC_T_VOID)
{
    static EC_T_BOOL s_bInitMsecCount = EC_FALSE;
    static struct timespec s_tsStart;
    struct timespec ts;
    struct timespec tsRes;
    EC_T_DWORD dwMsec;
    EC_T_DWORD dwNsecDelta;
    EC_T_DWORD dwSecDelta;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    if( !s_bInitMsecCount )
    {
        clock_getres(CLOCK_MONOTONIC, &tsRes);
        if( tsRes.tv_nsec > 1000000 )
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "OsQueryMsecCount - clock_getres resolution too small!\n"));
            OsDbgAssert(EC_FALSE);
        }
        s_tsStart.tv_nsec = ts.tv_nsec;
        s_tsStart.tv_sec = ts.tv_sec;
        s_bInitMsecCount = EC_TRUE;
    }
    if( ts.tv_nsec < s_tsStart.tv_nsec )
    {
        dwNsecDelta = s_tsStart.tv_nsec - ts.tv_nsec;
        dwSecDelta = (dwNsecDelta + 999999999)/1000000000;   /* round up to full seconds */
        ts.tv_sec -= dwSecDelta;
        ts.tv_nsec += 1000000000 * dwSecDelta;
    }
    OsDbgAssert( ts.tv_nsec >= s_tsStart.tv_nsec );
    ts.tv_nsec = ts.tv_nsec - s_tsStart.tv_nsec;
    ts.tv_sec = ts.tv_sec - s_tsStart.tv_sec;

    OsDbgAssert( ts.tv_sec >= 0 );
    OsDbgAssert( ts.tv_nsec <= 1000000000 );
    dwMsec = ts.tv_nsec / 1000000;
    dwMsec += 1000 * ts.tv_sec;
    return dwMsec;
}

/******************************************************************************/
/** \brief Open configuration file
*
* \return handle to configuration file or NULL in case of error
*/
EC_FNNAME EC_T_VOID* EC_FNCALL OsCfgFileOpen(const EC_T_CHAR* szCfgFileName)
{
    return fopen(szCfgFileName, "r");
}

/******************************************************************************/
/** \brief Close configuration file
*
* \return if no error has occurred, OsCfgFileClose returns 0. Otherwise, it returns a nonzero value.
*/
EC_FNNAME EC_T_INT EC_FNCALL OsCfgFileClose(EC_T_VOID* pvCfgFile)
{
    EC_T_INT nRet = 0;

    if( EC_NULL != pvCfgFile )
    {
        nRet = fclose( (FILE*)pvCfgFile );
    }

    return nRet;
}

/******************************************************************************/
/** \brief Read next chunk of the configuration file
*
* \return number of bytes read
*/
EC_FNNAME EC_T_INT EC_FNCALL OsCfgFileRead(EC_T_VOID* pvCfgFile, EC_T_VOID* pvDst, EC_T_INT nLen)
{
    EC_T_INT nRet = 0;

    if( EC_NULL != pvCfgFile )
    {
        nRet = fread(pvDst, 1, nLen, (FILE*)pvCfgFile);
    }

    return nRet;
}

/******************************************************************************/
/** \brief Determine if last OsCfgFileRead operation did cause an error
*
* \return if no error has occurred, OsCfgFileError returns 0. Otherwise, it returns a nonzero value.
*/
EC_FNNAME EC_T_INT EC_FNCALL OsCfgFileError(EC_T_VOID* pvCfgFile)
{
    return ferror((FILE*)pvCfgFile);
}

/******************************************************************************/
/** \brief Determine if the end of the configuration file is reached
*
* \return Returns 0 if the current position has not reached the end of the configuration file.
*/
EC_FNNAME EC_T_INT EC_FNCALL OsCfgFileEof(EC_T_VOID* pvCfgFile)
{
    return feof((FILE*)pvCfgFile);
}

#ifdef DEBUGTRACE
/******************************************************************************/
/** \brief OS Layer initialization.
*
* \return status code
*/
static EC_T_VOID OsOpenTraceFile(EC_T_VOID)
{
    if (S_pfTraceFile == EC_NULL)
    {
        S_pfTraceFile = fopen( TRACE_FILENAME, "wb" );
        if (S_pfTraceFile == EC_NULL)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "OsOpenTraceFile(): cannot open trace file %s\n", TRACE_FILENAME));
        }
        else
        {
            EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "************************************\n"));
            EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "TRACE ON: trace data stored in file: %s\n", TRACE_FILENAME));
            EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "************************************\n"));
        }
    }
}
#endif

/******************************************************************************/
/** \brief Print a trace message
*
* \return N/A
*/
#ifdef OsTrcMsg
#undef OsTrcMsg
#endif
EC_T_VOID OsTrcMsg(const EC_T_CHAR* szFormat, ...)
{
    EC_T_VALIST vaArgs;

    EC_VASTART(vaArgs, szFormat);
#ifdef DEBUGTRACE
    if (S_pfTraceFile == EC_NULL) OsOpenTraceFile();
    if (S_pfTraceFile != EC_NULL)
    {
        vfprintf( S_pfTraceFile, szFormat, vaArgs);
        fflush( S_pfTraceFile );
    }
    else
    {
        vprintf(szFormat, vaArgs);
    }
#else
    vprintf(szFormat, vaArgs);
#endif
    EC_VAEND(vaArgs);
}

/******************************************************************************/
/** \brief Assert proxy to halt on error and expose break point possibility
*/
EC_FNNAME EC_T_VOID EC_FNCALL OsDbgAssertFail(const EC_T_CHAR* szFile, EC_T_DWORD dwLine)
{
    EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: Assert failure in %s, line %d!\n", szFile, dwLine));
#if (defined OS_DBG_ASSERT_PAUSE)
    syslog(LOG_USER | LOG_ERR, "ASSERTION in file %s, line %d\n", szFile, (int)dwLine);
    pause();
#endif
}

/******************************************************************************/
/** \brief Assert proxy to halt on error and expose break point possibility
*/
EC_FNNAME EC_T_VOID EC_FNCALL OsDbgAssertFunc(EC_T_BOOL bAssertCondition, const EC_T_CHAR* szFile, EC_T_DWORD dwLine)
{
    if (!bAssertCondition)
    {
        OsDbgAssertFail(szFile, dwLine);
    }
}

/******************************************************************************/
/** \brief Create a synchronization mutual exclusion object
*
* \return handle to the mutex object.
*/
EC_API EC_T_VOID* EC_API_FNCALL OsCreateLock(EC_T_VOID)
{
    return OsCreateLockTyped(eLockType_DEFAULT);
}

/******************************************************************************/
/** \brief Create a synchronization mutual exclusion object
*
* \return handle to the mutex object.
*/
EC_API EC_T_VOID* EC_API_FNCALL OsCreateLockTyped(EC_T_OS_LOCK_TYPE eLockType)
{
    pthread_mutexattr_t MutexAttr;
    OS_LOCK_DESC* pvLockDesc = (OS_LOCK_DESC*)OsMalloc(sizeof(OS_LOCK_DESC));
    OsDbgAssert(EC_NULL != pvLockDesc);

    /* check if spin lock is active */

    if (pvLockDesc != NULL)
    {
      pthread_mutexattr_init(&MutexAttr);

      // Set the mutex as a recursive mutex
      pthread_mutexattr_settype(&MutexAttr, PTHREAD_MUTEX_RECURSIVE_NP);

      // create the muteIdx with the attributes set
      pthread_mutex_init(&pvLockDesc->Mutex, &MutexAttr);

      //After initializing the mutex, the thread attribute can be destroyed
      pthread_mutexattr_destroy(&MutexAttr);

      pvLockDesc->nLockCnt = 0;
      pvLockDesc->pThread = EC_NULL;
      pvLockDesc->eLockType = eLockType;
   }
    return pvLockDesc;
}

/******************************************************************************/
/** \brief Delete a mutex object
*
* \return N/A
*/
EC_API EC_T_VOID EC_API_FNCALL OsDeleteLock(EC_T_VOID* pvLockHandle)
{
    OS_LOCK_DESC* pvLockDesc = (OS_LOCK_DESC*)pvLockHandle;
    OsDbgAssert(EC_NULL != pvLockDesc);

    /* check if spin lock is active */

    if (pvLockDesc != NULL)
    {
        OsDbgAssert( pvLockDesc->nLockCnt == 0 );
        pthread_mutex_destroy( &pvLockDesc->Mutex );
    }
    SafeOsFree(pvLockDesc);
}

/******************************************************************************/
EC_API EC_T_VOID EC_API_FNCALL OsLock(EC_T_VOID* pvLockHandle)
{
    OS_LOCK_DESC* pvLockDesc = (OS_LOCK_DESC*)pvLockHandle;

    OsDbgAssert(pvLockDesc != EC_NULL);

    if (pvLockDesc != NULL)
    {
        pthread_mutex_lock(&(pvLockDesc->Mutex));
#if (defined DEBUG)
        pvLockDesc->pThread = pthread_self();
        if (pvLockDesc->eLockType == eLockType_SPIN)
        {
            S_bSpinLockActive = EC_TRUE;
            S_SpinLockThread = pvLockDesc->pThread;
        }
        /* it is not allowed to call oslock inside the job task with a lock type not equal to spin */
        else if (S_JobTaskThread == pvLockDesc->pThread)
        {
            OsDbgAssert(EC_FALSE);
        }
        pvLockDesc->nLockCnt++;
        OsDbgAssert(pvLockDesc->nLockCnt < 10);  /* that much nesting levels? */
#endif
    }
}

/******************************************************************************/
EC_API EC_T_VOID EC_API_FNCALL OsUnlock(EC_T_VOID* pvLockHandle)
{
    OS_LOCK_DESC* pvLockDesc = (OS_LOCK_DESC*)pvLockHandle;

    OsDbgAssert( pvLockDesc != EC_NULL );

    if (pvLockDesc != NULL)
    {
#ifdef DEBUG
        pvLockDesc->nLockCnt--;
        if (pvLockDesc->nLockCnt == 0)
        {
            pvLockDesc->pThread = EC_NULL;
        }
        OsDbgAssert(pvLockDesc->nLockCnt >= 0);

        if(pvLockDesc->eLockType == eLockType_SPIN)
        {
            S_bSpinLockActive = EC_FALSE;
            S_SpinLockThread = EC_NULL;
        }

#endif
        pthread_mutex_unlock(&(pvLockDesc->Mutex));
    }
}

/********************************************************************************/
/** \brief Create a binary semaphore
*
* \return event object to be referenced in further calls or EC_NULL in case of errors.
*/
EC_API EC_T_VOID* EC_API_FNCALL OsCreateEvent(EC_T_VOID)
{
    sem_t* pSemaphore;
    EC_T_VOID* pvRetVal = EC_NULL;

    /* create event */
    pSemaphore = (sem_t*)OsMalloc(sizeof(sem_t));
    if( pSemaphore == NULL )
    {
        goto Exit;
    }
    OsMemset(pSemaphore, 0, sizeof(sem_t));
    if (sem_init(pSemaphore, 0, 0) < 0)
    {
        perror("sem_init failed");
        goto Exit;
    }

    /* no errors */
    pvRetVal = (EC_T_VOID*)pSemaphore;

Exit:

    return pvRetVal;
}

/********************************************************************************/
/** \brief  Delete an event object
*
* \return N/A.
*/
EC_API EC_T_VOID EC_API_FNCALL OsDeleteEvent(EC_T_VOID* pvEvent)
{
    /* delete event */
    sem_destroy((sem_t*)pvEvent);
    SafeOsFree(pvEvent);
}

/********************************************************************************/
/** \brief  Set event
*/
EC_API EC_T_VOID EC_API_FNCALL OsPlatformImplSetEvent(EC_T_VOID* pEvent)
{
    int iRes = 0;
    int iVal = 0;

    /* Ensure the event trigger count does not exceed one per cycle. */
    iRes = sem_getvalue((sem_t*)pEvent, &iVal);
    if ((0 == iRes) && (iVal > 0))
        return;

    sem_post((sem_t*)pEvent);
}

/********************************************************************************/
/** \brief  Wait for event
*
* \return EC_E_NOERROR if event was set for the timeout, or error code in case of errors.
*/
EC_API EC_T_DWORD EC_API_FNCALL OsWaitForEvent(EC_T_VOID* pvEvent, EC_T_DWORD dwTimeout)
{
    int nResult;
    struct timespec tsCurr;
    struct timespec tsLatest;
    EC_T_DWORD dwRetVal = EC_E_ERROR;

    /* handle different timeout cases */
    switch (dwTimeout)
    {
    case EC_NOWAIT:
        nResult = sem_trywait( (sem_t*)pvEvent );
        if (nResult == 0)
        {
            dwRetVal = EC_E_NOERROR;
        }
        else if ((nResult == -1) && (errno == EAGAIN))
        {
            dwRetVal = EC_E_TIMEOUT;
        }
        else
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "OsWaitForEvent: ERROR: sem_trywait returned %d\n",
                    nResult));
            OsDbgAssert(EC_FALSE);
            dwRetVal = EC_E_ERROR;
        }
        break;

    case EC_WAITINFINITE:
        nResult = sem_wait((sem_t*)pvEvent);
        if (nResult == 0)
        {
            dwRetVal = EC_E_NOERROR;
        }
        else
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "OsWaitForEvent: ERROR: sem_wait returned %d\n",
                    nResult));
            OsDbgAssert(EC_FALSE);
            dwRetVal = EC_E_ERROR;
        }
        break;

    default:
        /* Calculate relative interval as current time plus number of milliseconds */
        if (clock_gettime(CLOCK_REALTIME, &tsCurr) == -1)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "OsWaitForEvent: clock_gettime returned an error\n"));
            OsDbgAssert(EC_FALSE);
        }
        tsLatest.tv_sec = dwTimeout / 1000;
        tsLatest.tv_nsec = (dwTimeout % 1000) * 1000000;

        tsLatest.tv_sec += tsCurr.tv_sec;
        tsLatest.tv_nsec += tsCurr.tv_nsec;
        if (tsLatest.tv_nsec >= 1000000000)
        {
           ++tsLatest.tv_sec;
           tsLatest.tv_nsec -= 1000000000;
        }
        OsDbgAssert(tsLatest.tv_nsec < 1000000000);

        nResult = sem_timedwait((sem_t*)pvEvent, &tsLatest);
        if (nResult == 0)
        {
            dwRetVal = EC_E_NOERROR;
        }
        else if ((nResult == -1) && (errno == ETIMEDOUT))
        {
            dwRetVal = EC_E_TIMEOUT;
        }
        else
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "OsWaitForEvent: sem_timedwait returned error %d\n", nResult));
            OsDbgAssert(EC_FALSE);
            dwRetVal = EC_E_ERROR;
        }
        break;
    }

    return dwRetVal;
}

/********************************************************************************/
/** \brief Determine type of OsSleep implementation.
*
* \return N/A.
*/
EC_T_VOID   OsSleepSetType(E_SLEEP ESleep)
{
    S_ESleep = ESleep;
}

/********************************************************************************/
/** \brief Sleep for a specified amount of time.
*
* \return N/A.
*/
EC_T_VOID  OsPlatformImplSleep(EC_T_DWORD dwMsec)
{
    struct timespec ts;
    OsMemset(&ts, 0, sizeof(struct timespec));

    switch (S_ESleep)
    {
    case eSLEEP_USLEEP:
        usleep(1000 * dwMsec);
        break;

    case eSLEEP_NANOSLEEP:
        ts.tv_sec = dwMsec / 1000;
        ts.tv_nsec = 1000000 * (dwMsec % 1000);
        nanosleep(&ts, EC_NULL);
        break;

    case eSLEEP_CLOCK_NANOSLEEP:
    default:
        ts.tv_sec = dwMsec / 1000;
        ts.tv_nsec = 1000000 * (dwMsec % 1000);
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, EC_NULL);
        break;
    }
}

static void* ThreadStub(ThreadArg* arg)
{
   /* Set threadname (max 16 bytes) */
   prctl(PR_SET_NAME, arg->name, 0, 0, 0);

   /* Set CPU affinity mask */
   if (arg->cpuMask != 0)
   {
      OsSetThreadAffinity(EC_NULL, arg->cpuMask);
   }

   /* Try to set up realtime scheduling. This will probably fail if the kernel
    * has no preemption support compiled in.
    */
   struct sched_param schedParam = { 0 };
   schedParam.sched_priority = arg->threadPrio;
   if (sched_setscheduler(0, SCHED_FIFO, &schedParam) < 0)
   {
       EcLogMsg(EC_LOG_LEVEL_WARNING, (pEcLogContext, EC_LOG_LEVEL_WARNING, "WARNING: Couldn't change to realtime scheduling class (SCHED_FIFO).\n"));
   }

#if defined(DEBUG) && 1 /* Print scheduling info about the thread */

   /* Query scheduling parameters of calling thread */
   schedParam.sched_priority = 0;
   sched_getparam(0, &schedParam);

   /* Query the CPU of calling thread */
   int cpuIdx = sched_getcpu();

   /* Query CPU's on whoes the calling thread is allowed to run */
   cpu_set_t cpuSet;
   CPU_ZERO(&cpuSet);
   if (pthread_getaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet) < 0)
   {
       EcLogMsg(EC_LOG_LEVEL_WARNING, (pEcLogContext, EC_LOG_LEVEL_WARNING, "pthread_getaffinity_np failed\n"));
   }

   /* Query scheduling policy of calling thread */
   int schedPolicy = sched_getscheduler(0);
   const char *schedPolicyName = "UNKNOWN";
   switch (schedPolicy)
   {
     case SCHED_FIFO: schedPolicyName = "SCHED_FIFO"; break;
     case SCHED_RR: schedPolicyName = "SCHED_RR"; break;
     case SCHED_OTHER: schedPolicyName = "SCHED_OTHER"; break;
     case SCHED_BATCH: schedPolicyName = "SCHED_BATCH"; break;
   }

   /* Query scheduling timer resolution */
   struct timespec tsRes = { 0 };
   clock_getres(schedPolicy, &tsRes);

   EcLogMsg(EC_LOG_LEVEL_VERBOSE, (pEcLogContext, EC_LOG_LEVEL_VERBOSE, "   >>> [OSAL] Thread '%s' created. Scheduler: %s, Prio %d, ClockRes: %u, CPU: %d, CPUAffinityMask: 0x%x\n",
         arg->name,
         schedPolicyName,
         schedParam.sched_priority,
         tsRes.tv_nsec,
         cpuIdx,
         CpuSetToMask(cpuSet)));

#endif

   /* Call thread entry */
   arg->entry(arg->arg);

   delete arg;

   return NULL;
}

/********************************************************************************/
/** \brief Create thread.
*
* \return thread object or EC_NULL in case of an error.
*/
EC_API EC_T_VOID* EC_API_FNCALL OsPlatformImplCreateThread(
                                       EC_T_CHAR* szThreadName,
                                       EC_PF_THREADENTRY pfThreadEntry,
                                       EC_T_DWORD dwPrio,
                                       EC_T_DWORD dwStackSize,
                                       EC_T_VOID* pvParams
                                       )
{
    int nResult;
    pthread_t pThread;
    pthread_attr_t ThreadAttr;
    EC_T_VOID* pvThreadObject = EC_NULL;
    ThreadArg* arg = new ThreadArg;
    memset(arg, 0, sizeof(ThreadArg));

#if defined(DEBUG) && 0
    EcLogMsg(EC_LOG_LEVEL_VERBOSE, (pEcLogContext, EC_LOG_LEVEL_VERBOSE, "Create thread: Name '%s', Prio %d, CpuMask %d, Stack %d\n",
          szThreadName,
          dwPrio & 0xFFFF,
          dwPrio >> 16,
          dwStackSize);
#endif

    /*
     *  set thread attributes
     */
    nResult = pthread_attr_init(&ThreadAttr);
    if (0 != nResult)
    {
       EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Create thread: pthread_attr_init() failed. Error: %d\n",
          nResult));
       goto FatalExit;
    }
    /* Set the stack size of the thread */
    dwStackSize = (dwStackSize < PTHREAD_STACK_MIN) ? PTHREAD_STACK_MIN : PAGE_UP(dwStackSize);
    nResult = pthread_attr_setstacksize(&ThreadAttr, dwStackSize);
    if (0 != nResult)
    {
       EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Create thread: pthread_attr_setstacksize() failed. Error: %d\n",
          nResult));
       goto FatalExit;
    }
    /* Set thread to detached state. No need for pthread_join*/
    nResult = pthread_attr_setdetachstate(&ThreadAttr, PTHREAD_CREATE_DETACHED);
    if (0 != nResult)
    {
       EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Create thread: pthread_attr_setdetachstate() failed. Error: %d\n",
          nResult));
       goto FatalExit;
    }
    nResult = pthread_attr_setinheritsched(&ThreadAttr, PTHREAD_EXPLICIT_SCHED);
    if (0 != nResult)
    {
       EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Create thread: pthread_attr_setinheritsched() failed. Error: %d\n",
          nResult));
       goto FatalExit;
    }
    /*
     * Fill arguments for thread stub
     */
    arg->entry = pfThreadEntry;
    arg->arg = pvParams;
    strncpy(arg->name, szThreadName, sizeof(arg->name) - 1);
    arg->name[MAX_THREADNAME_LEN - 1] = '\0';
    arg->threadPrio = dwPrio & 0xFFFF;
    arg->cpuMask = dwPrio >> 16; /* Use high word of priority as CPU mask */

    /*
     *  create the thread
     */
    nResult = pthread_create( &pThread, &ThreadAttr, (void*(*)(void*))ThreadStub, arg );
    if(0 != nResult)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "Create thread: pthread_create() failed. Error: %d\n",
           nResult));
       goto FatalExit;
    }

    pvThreadObject = (EC_T_VOID*)pThread;

    /* is this the job task ? */
    if( OsStrcmp(szThreadName, "tEcJobTask") == 0)
    {
        S_JobTaskThread = pThread;
    }

    goto Exit;

FatalExit:
    delete arg;
Exit:
    pthread_attr_destroy( &ThreadAttr );
    return pvThreadObject;
}

/***************************************************************************************************/
/** \brief  Delete a thread Handle returned by OsCreateThread.
*
* \return N/A.
*/
EC_API EC_T_VOID EC_API_FNCALL OsPlatformImplDeleteThreadHandle(
    EC_T_VOID* pvThreadObject       /**< [in]   Previously allocated Thread Handle */
                                            )
{
    /* reset job task handle */
    if ((EC_T_VOID*)S_JobTaskThread == pvThreadObject)
    {
        S_JobTaskThread = EC_NULL;
    }
}

/********************************************************************************/
/** \brief Set thread priority.
*
* \return N/A.
*/
EC_API EC_T_VOID EC_API_FNCALL OsSetThreadPriority(EC_T_VOID* pvThreadObject, EC_T_DWORD dwPrio)
{
    pthread_attr_t ThreadAttr;
    int nSchedPolicy = SCHED_FIFO;    // scheduling policy - real time
    struct sched_param SchedParam;  // scheduling priority

    /* set thread attributes */
    /*************************/
    pthread_attr_init( &ThreadAttr );
    /* Set the policy of the thread to real time*/
    pthread_attr_setschedpolicy( &ThreadAttr, nSchedPolicy);
    /* Set the scheduling priority of the thread - (1 = lowest, 99 = highest) */
    SchedParam.sched_priority = dwPrio;
    pthread_attr_setschedparam( &ThreadAttr, &SchedParam );

    /* change thread priority */
    /**************************/
    pthread_setschedparam( (pthread_t)pvThreadObject, nSchedPolicy, &SchedParam );

    pthread_attr_destroy( &ThreadAttr );
}

/********************************************************************************/
/** \brief Set thread affinity.
 *
 * \return EC_TRUE if successful, EC_FALSE otherwise.
 */
EC_API EC_T_BOOL EC_API_FNCALL OsSetThreadAffinity(EC_T_VOID* pvThreadObject, EC_T_CPUSET cpuMask)
{
    pthread_t thread = (pvThreadObject == EC_NULL)
         ? pthread_self()
         : (pthread_t) pvThreadObject;

    cpu_set_t cpuSet = CpuMaskToSet(cpuMask);
    if (pthread_setaffinity_np(thread, sizeof(cpuSet), &cpuSet) < 0)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "pthread_setaffinity_np failed\n"));
        return EC_FALSE;
    }

    return EC_TRUE;
}

/********************************************************************************/
/** \brief Get thread affinity.
*
* \return EC_TRUE if successful, EC_FALSE otherwise.
*/
EC_API EC_T_BOOL EC_API_FNCALL OsGetThreadAffinity(EC_T_VOID* pvThreadObject, EC_T_CPUSET* pCpuSet)
{
    pthread_t thread;
    if (pvThreadObject == NULL) thread = pthread_self();
    else                        thread = (pthread_t) pvThreadObject;

    if (pthread_getaffinity_np(thread, sizeof(EC_T_CPUSET), (cpu_set_t *) pCpuSet) < 0)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "pthread_getaffinity_np failed\n"));
        return EC_FALSE;
    }

    return EC_TRUE;
}

/***************************************************************************************************/
/**
\brief  Replaces the built in Link Layer Driver Registration function

\return N/A
*/
EC_T_VOID OsPlatformImplReplaceGetLinkLayerRegFunc(EC_PF_GETLINKLAYERREGFUNC pfGetLinkLayerRegFunc)
{
    S_pfGetLinkLayerRegFunc = pfGetLinkLayerRegFunc;
}

/***************************************************************************************************/
/**
\brief  Get Link Layer Driver Registration function

This function returns the link layer registration function.
\return link layer registration function.
*/
EC_PF_LLREGISTER OsPlatformImplGetLinkLayerRegFunc(EC_T_CHAR* szDriverIdent)
{
    EC_T_CHAR           szTmpName[MAX_SODIR_LEN];
    EC_PF_LLREGISTER    pfLlRegister = EC_NULL;
    void*               pvLibHandle = EC_NULL;
    void*               pvSymbol = EC_NULL;
    char*               szErrorInfo = EC_NULL;
    int                 i = 0;
    EC_T_BOOL           bLoadSucceeded = EC_FALSE;

    if (EC_NULL != S_pfGetLinkLayerRegFunc)
    {
        pfLlRegister = (*S_pfGetLinkLayerRegFunc)(szDriverIdent);
        goto Exit;
    }
    
    /* Clear any pending error message */
    dlerror();

    /* search LinkLayer first in current working directory and then with ld library search path */
    for (i = 0; i < 2; i++)
    {
        szTmpName[0] = '\0';
        /* first look in current working directory */
        if ((0 == i) && (EC_NULL != getcwd(szTmpName, MAX_SODIR_LEN)))
        {
            strncat(szTmpName, "/", MAX_SODIR_LEN - OsStrlen(szTmpName) - 1);
        }
        strncat(szTmpName, "libemll", MAX_SODIR_LEN - OsStrlen(szTmpName) - 1);
        strncat(szTmpName, szDriverIdent, MAX_SODIR_LEN - OsStrlen(szTmpName) - 1);
        strncat(szTmpName, ".so", MAX_SODIR_LEN - OsStrlen(szTmpName) - 1);
        EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "OsGetLinkLayerRegFunc: try to load '%s'\n",
                szTmpName));

        pvLibHandle = dlopen(szTmpName, RTLD_LAZY | RTLD_GLOBAL);
        if (EC_NULL == pvLibHandle)
        {
            szErrorInfo = dlerror();
            if (EC_NULL != szErrorInfo)
            {
                EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "OsGetLinkLayerRegFunc: dlopen returned error '%s'\n",
                    szErrorInfo));
            }
        }
        else
        {
           bLoadSucceeded = EC_TRUE;
           break;
        }
    }
    if (!bLoadSucceeded)
    {
        goto Exit;
    }
    strncpy(szTmpName, "emllRegister", MAX_SODIR_LEN - 1);
    strncat(szTmpName, szDriverIdent, MAX_SODIR_LEN - OsStrlen(szTmpName) - 1);

    /* Clear any pending error message */
    dlerror();

    /* Look for symbol in the shared lib */
    pvSymbol = dlsym(pvLibHandle, szTmpName);
    if (EC_NULL == pvSymbol)
    {
        szErrorInfo = dlerror();
        if (EC_NULL != szErrorInfo)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: dlsym returned error '%s' when looking for symbol '%s'!\n",
                szErrorInfo, szTmpName));
            goto Exit;
        }
        else
        {
            /* symbol found, but is NULL! */
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: dlsym returned NULL pointer when looking for symbol '%s'!\n",
                szTmpName));
            goto Exit;
        }
    }

    pfLlRegister = (EC_PF_LLREGISTER)pvSymbol;
Exit:
    return pfLlRegister;
}

#if (defined INCLUDE_OSPERF)
/*******************************************************************************
 *
 * plTimestampIsr - performance logging timestamp ISR
 *
 */
EC_T_VOID              /* Return: N/A */
plTimestampIsr(
    int nArg       /* argument given in sysTimestampConnect */
              )
{
    S_qwLastPerfTime = EC_MAKEQWORD(EC_HIDWORD(S_qwLastPerfTime)+1, EC_LODWORD(S_qwLastPerfTime));
}
#endif

/***************************************************************************************************/
/**
\brief  Get current system time in nanoseconds

\return error code, EC_E_NOERROR in case of success
*/
EC_API EC_T_DWORD EC_API_FNCALL OsSystemTimeGet(EC_T_UINT64* pqwSystemTime)
{
    EC_T_DWORD dwRetVal = EC_E_ERROR;
    EC_T_DWORD dwRes    = EC_E_ERROR;

    /* check parameters */
    if (EC_NULL == pqwSystemTime)
    {
        dwRetVal = EC_E_INVALIDPARM;
        goto Exit;
    }
    /* get system time */
    if( EC_NULL != S_pfSystemTimeGet )
    {
        /* from configured function */
        dwRes = S_pfSystemTimeGet(pqwSystemTime);
        if (EC_E_NOERROR != dwRes)
        {
            dwRetVal = dwRes;
            goto Exit;
        }
    }
    else
    {
        /* from default */
        struct timespec ts;
        EC_T_UINT64 qwSec = 0;
        EC_T_UINT64 qwNsec = 0;

        clock_gettime(CLOCK_REALTIME, &ts);

        EC_SETQWORD(&qwSec, (EC_T_UINT64)ts.tv_sec);

        /* year 1970 (UNIX epoche) vs. 2000 (EtherCAT epoche) */
        qwSec = qwSec - 946684800ul;

        EC_SETQWORD(&qwNsec, (EC_T_UINT64)ts.tv_nsec);
        qwNsec = qwNsec + qwSec * 1000000000ul;

        EC_SETQWORD(pqwSystemTime, qwNsec);
    }

    /* no errors */
    dwRetVal = EC_E_NOERROR;

Exit:
    return dwRetVal;
}

static EC_T_BOOL   S_bCalibrated       = EC_FALSE;
static EC_T_UINT64 S_dwlHzFrequency    = 0;        // frequency in Hz
static EC_T_DWORD  S_dw100kHzFrequency = 0;        // frequency in 100 kHz units (e.g. 1MHz = 10)

/***************************************************************************************************/
/**
@brief  OsMeasCalibrate

Measure the TSC frequency for timing measurements.
This function must be called before using one of the following functions:
*/
EC_API EC_T_VOID EC_API_FNCALL OsMeasCalibrate(EC_T_UINT64 dwlFreqSet)
{
    EC_T_INT nThreadPriority = 0;

    if( !S_bCalibrated )
    {
        if( dwlFreqSet == 0 )
        {
            /* auto calibrate */
            EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "Calibrate tsc measurement... "));

            EC_UNREFPARM(nThreadPriority);
            {
                EC_T_UINT64 dwlTimeStamp1 = 0;
                EC_T_UINT64 dwlTimeStamp2 = 0;
                EC_T_UINT64 dwlTimeStamp3 = 0;

                OsSleep( 1 );
                dwlTimeStamp1  = OsMeasGetCounterTicks();
                OsSleep( 1000 );
                dwlTimeStamp2  = OsMeasGetCounterTicks();
                OsSleep( 2000 );
                dwlTimeStamp3  = OsMeasGetCounterTicks();

                S_dwlHzFrequency = ((dwlTimeStamp3 - dwlTimeStamp2)/2 + (dwlTimeStamp2 - dwlTimeStamp1))/2;
            }
            EcLogMsg(EC_LOG_LEVEL_INFO, (pEcLogContext, EC_LOG_LEVEL_INFO, "done: %d MHz\n",
                (EC_T_INT)(S_dwlHzFrequency/1000000)));
        }
        else
        {
            S_dwlHzFrequency = dwlFreqSet;
        }
        S_dw100kHzFrequency = (EC_T_DWORD)(S_dwlHzFrequency / 100000);
    }
    else if( dwlFreqSet != 0 )
    {
        /* two different frequencies should not happen */
        OsDbgAssert( dwlFreqSet == S_dwlHzFrequency );
    }
    S_bCalibrated = EC_TRUE;
}

/***************************************************************************************************/
/**
@brief  OsMeasGet100kHzFrequency

@return the frequency of the TSC in 100kHz multiples [EC_T_DWORD]
*/
EC_API EC_T_DWORD EC_API_FNCALL OsMeasGet100kHzFrequency(EC_T_VOID)
{
    EC_T_DWORD dwRetVal = 0;

    if (!S_bCalibrated)
    {
        EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "TscGet100kHzFrequency() TSC not calibrated\n"));
        goto Exit;
    }

    dwRetVal = S_dw100kHzFrequency;

Exit:
    return dwRetVal;
}

/***************************************************************************************************/
/**
@brief  OsMeasGetCounterTicks

@return the current TSC count [EC_T_UINT64]
*/
EC_API EC_T_UINT64 EC_API_FNCALL OsMeasGetCounterTicks(EC_T_VOID)
{
    EC_T_DWORD   dwTimeStampLo = 0;
    EC_T_DWORD   dwTimeStampHi = 0;

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile (
             "rdtsc"
             : "=a" (dwTimeStampLo), "=d" (dwTimeStampHi)
             );
#elif defined(__GNUC__) && defined(__PPC__)
    {
       EC_T_DWORD dwTmp = 0;

       /* Read PowerPC architecture's time base register.
        * The following code reads the high dword 2 times and
        * loops if they are not equal (check for rollover).
        */
      __asm__ volatile (
                 "0:                   \n"
                 "\tmftbu   %0         \n" /* Read high word (dwTimeStampHi) */
                 "\tmftb    %1         \n" /* Read low word (dwTimeStampLo) */
                 "\tmftbu   %2         \n" /* Read high word again (dwTmp) */
                 "\tcmpw    %2,%0      \n" /* compare (dwTimeStampHi) and (dwTmp). Check for rollover */
                 "\tbne     0b         \n" /* jump if not equal to label 0: */
                 : "=r"(dwTimeStampHi),"=r"(dwTimeStampLo),"=r"(dwTmp)
                 );
    }
#else

    struct timespec TimeStamp;
    clock_gettime(CLOCK_MONOTONIC, &TimeStamp);

    EC_T_UINT64 qwTimeStampNsec = TimeStamp.tv_nsec;
    EC_T_UINT64 qwTimeStampSec  = TimeStamp.tv_sec;
    EC_T_UINT64 qwTimeStamp     = qwTimeStampNsec + qwTimeStampSec * 1000000000;

    return qwTimeStamp;

#endif
    return EC_MAKEQWORD(dwTimeStampHi, dwTimeStampLo);
}

/********************************************************************************/
/* \brief Set event according to periodical sleep
 * Cyclically sets an event for thread synchronization purposes.
 * Wait for IRQ, acknowledge IRQ, SetEvent in loop until shutdown
 * Return: N/A
 */
static EC_T_VOID AuxClkTask(EC_T_VOID* pvThreadParamDesc)
{
    EC_T_AUXCLK_DESC* pDesc = &S_AuxDesc;
    EC_T_CPUSET       CpuSet;
    struct timespec t;
    OsMemset(&CpuSet, 0, sizeof(EC_T_CPUSET));
    OsMemset(&t, 0, sizeof(struct timespec));

    EC_CPUSET_ZERO(CpuSet);
    EC_CPUSET_SET(CpuSet, pDesc->dwCpuIndex);
    OsSetThreadAffinity(EC_NULL, CpuSet);

    /* get current time */
    clock_gettime(CLOCK_MONOTONIC, &t);

    /* start after one second */
    t.tv_sec = t.tv_sec + 1;

    /* timing task started */
    pDesc->bIsRunning = EC_TRUE;

    /* periodically generate events as long as the application runs */
    while (!pDesc->bShutdown)
    {
        /* wait for the next cycle */
        /* Use the Linux high resolution timer. This API offers resolution
         * below the systick (i.e. 50us cycle is possible) if the Linux
         * kernel is patched with the RT-PREEMPT patch.
         */

        /* wait until next shot */
        if (0 != clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL))
        {
           perror("clock_nanosleep failed");
           pDesc->bShutdown = EC_TRUE;
        }

        /* trigger jobtask */
        OsSetEvent(pDesc->pvTimingEvent);

        /* calculate next shot. t.tv_nsec is always < 1000000000 */
        t.tv_nsec = t.tv_nsec + pDesc->nCycleTimeNsec;

        /* norm time */
        while (t.tv_nsec >= NSEC_PER_SEC)
        {
           t.tv_nsec = t.tv_nsec - NSEC_PER_SEC;
           t.tv_sec++;
        }
    }

    pDesc->bIsRunning = EC_FALSE;
}

EC_API EC_T_DWORD EC_API_FNCALL OsAuxClkInit(EC_T_DWORD dwCpuIndex, EC_T_DWORD dwFrequencyHz, EC_T_VOID* pvOsEvent)
{
    OsMemset(&S_AuxDesc, 0, sizeof(EC_T_AUXCLK_DESC));
    S_AuxDesc.dwCpuIndex = dwCpuIndex;
    S_AuxDesc.pvTimingEvent = pvOsEvent;
    S_AuxDesc.nCycleTimeNsec = NSEC_PER_SEC / dwFrequencyHz;

    /* verify cycle time and patch if needed */
    if (S_AuxDesc.nCycleTimeNsec == 0)
        S_AuxDesc.nCycleTimeNsec = 1;

    S_AuxDesc.nOrigCycleTimeNsec = S_AuxDesc.nCycleTimeNsec;
    S_AuxDesc.bShutdown = EC_FALSE;
    S_AuxDesc.bIsRunning = EC_FALSE;

    OsCreateThread((EC_T_CHAR*)"tEcAuxClkTask", (EC_PF_THREADENTRY)AuxClkTask,
            TIMER_THREAD_PRIO, LOG_THREAD_STACKSIZE, EC_NULL);
    while (!S_AuxDesc.bIsRunning)
    {
        OsSleep(1);
    }

    return EC_E_NOERROR;
}

EC_T_DWORD OsAuxClkDeinit(EC_T_VOID)
{
    S_AuxDesc.bShutdown = EC_TRUE;

    /* wait until finished for 10 cycles */
    OsSleep(EC_MAX(10 * S_AuxDesc.nOrigCycleTimeNsec / 1000000, 1));

    return EC_E_NOERROR;
}

EC_T_DWORD OsHwTimerGetInputFrequency(EC_T_DWORD *pdwFrequencyHz)
{
    if ((EC_NULL != pdwFrequencyHz) && S_AuxDesc.bIsRunning)
    {
        EC_SETDWORD(pdwFrequencyHz, NSEC_PER_SEC / S_AuxDesc.nCycleTimeNsec);
    }
    return EC_E_NOERROR;
}

EC_T_DWORD OsHwTimerModifyInitialCount(EC_T_INT nAdjustPermil)
{
    /* modify existing sleep cycle duration */
    if (S_AuxDesc.bIsRunning)
    {
        EC_T_INT nAdjustment = (S_AuxDesc.nOrigCycleTimeNsec * nAdjustPermil) / 1000;
        S_AuxDesc.nCycleTimeNsec = S_AuxDesc.nOrigCycleTimeNsec + nAdjustment;
    }

    return EC_E_NOERROR;
}

#if (defined EC_SOCKET_IP_SUPPORTED)
EC_API EC_T_DWORD EC_API_FNCALL OsSocketInit(EC_T_VOID)
{
    return EC_E_NOERROR;
}

EC_API EC_T_DWORD EC_API_FNCALL OsSocketDeInit(EC_T_VOID)
{
    return EC_E_NOERROR;
}
#endif /* EC_SOCKET_IP_SUPPORTED */

/*-END OF SOURCE FILE--------------------------------------------------------*/

