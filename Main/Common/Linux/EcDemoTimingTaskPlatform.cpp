/*-----------------------------------------------------------------------------
 * EcDemoTimingTaskPlatform.cpp
 * Copyright                acontis technologies GmbH, Ravensburg, Germany
 * Response                 Timo Nussbaumer
 * Description              EtherCAT demo timing task
 *----------------------------------------------------------------------------*/

#include "EcDemoTimingTaskPlatform.h"
#include "EcType.h"
#include "EcLogging.h"
#include "EcDemoPlatform.h"

#define NSEC_PER_SEC                (1000000000)

CDemoTimingTaskPlatform::CDemoTimingTaskPlatform()
    : TBaseClass()
{
}

CDemoTimingTaskPlatform::CDemoTimingTaskPlatform(_T_EC_DEMO_APP_CONTEXT& rAppContext)
    : TBaseClass(rAppContext)
{
}

EC_T_VOID CDemoTimingTaskPlatform::TimingTask()
{
    EC_T_CPUSET       CpuSet;
    struct timespec t;
    OsMemset(&CpuSet, 0, sizeof(EC_T_CPUSET));
    OsMemset(&t, 0, sizeof(struct timespec));

    EC_CPUSET_ZERO(CpuSet);
    EC_CPUSET_SET(CpuSet, this->m_dwCpuIndex);
    OsSetThreadAffinity(EC_NULL, CpuSet);

    /* get current time */
    clock_gettime(CLOCK_MONOTONIC, &t);

    t.tv_nsec = t.tv_nsec + this->m_nCycleTimeNsec;

    /* norm time */
    while (t.tv_nsec >= NSEC_PER_SEC)
    {
        t.tv_nsec = t.tv_nsec - NSEC_PER_SEC;
        t.tv_sec++;
    }

    /* timing task started */
    this->m_bIsRunning = EC_TRUE;

    /* periodically generate events as long as the application runs */
    while (!this->m_bShutdown)
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
           this->m_bShutdown = EC_TRUE;
        }

        /* trigger jobtask */
        this->SetTimingEvent();

        /* calculate next shot. t.tv_nsec is always < 1000000000 */
        t.tv_nsec = t.tv_nsec + this->m_nCycleTimeNsec;

        /* norm time */
        while (t.tv_nsec >= NSEC_PER_SEC)
        {
           t.tv_nsec = t.tv_nsec - NSEC_PER_SEC;
           t.tv_sec++;
        }
    }

    this->m_bIsRunning = EC_FALSE;
}

EC_T_DWORD CDemoTimingTaskPlatform::GetHostTime(EC_T_VOID* pvContext, EC_T_UINT64* pnActualHostTimeInNsec)
{
#if 1
    return TBaseClass::GetHostTime(pvContext, pnActualHostTimeInNsec);
#else
    /* TBaseClass::GetHostTime calls OsSystemTimeGet and this implementation corresponds to */
    struct timespec ts;
    EC_T_UINT64 qwSec = 0;
    EC_T_UINT64 qwNsec = 0;

    clock_gettime(CLOCK_REALTIME, &ts);

    qwSec = (EC_T_UINT64)ts.tv_sec;

    /* year 1970 (UNIX epoche) vs. 2000 (EtherCAT epoche) */
    qwSec = qwSec - 946684800ul;

    qwNsec = (EC_T_UINT64)ts.tv_nsec;
    qwNsec = qwNsec + qwSec * 1000000000ul;

    *pnActualHostTimeInNsec = qwNsec;
#endif
}
