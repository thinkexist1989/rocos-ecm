/*-----------------------------------------------------------------------------
 * EcDemoTimingTask.cpp
 * Copyright                acontis technologies GmbH, Ravensburg, Germany
 * Response                 Timo Nussbaumer
 * Description              EtherCAT demo timing task
 *----------------------------------------------------------------------------*/

#include "EcDemoTimingTask.h"
#include "EcLogging.h"
#include "EcDemoPlatform.h"
#include "EcDemoParms.h" //T_EC_DEMO_APP_CONTEXT

CDemoTimingEvent::CDemoTimingEvent()
    : m_pvTimingEvent(EC_NULL)
{
}

CDemoTimingEvent::~CDemoTimingEvent()
{
    this->Delete();
}

EC_T_DWORD CDemoTimingEvent::Create()
{
    /* create the timing event if not already created */
    if (this->m_pvTimingEvent == EC_NULL)
    {
        this->m_pvTimingEvent = OsCreateEvent();
        if (this->m_pvTimingEvent == EC_NULL)
        {
            EcLogMsg(EC_LOG_LEVEL_ERROR, (pEcLogContext, EC_LOG_LEVEL_ERROR, "ERROR: insufficient memory to create timing event!\n"));
            return EC_E_NOMEMORY;
        }
    }
    return EC_E_NOERROR;
}

EC_T_DWORD CDemoTimingEvent::Set()
{
    if (this->m_pvTimingEvent != EC_NULL)
    {
        OsSetEvent(this->m_pvTimingEvent);
        return EC_E_NOERROR;
    }
    else
    {
        return EC_E_ERROR;
    }
}

EC_T_VOID CDemoTimingEvent::Delete()
{
    /* delete the timing event */
    if (this->m_pvTimingEvent != EC_NULL)
    {
        OsDeleteEvent(this->m_pvTimingEvent);
        this->m_pvTimingEvent = EC_NULL;
    }
}

EC_T_VOID* CDemoTimingEvent::GetRaw()
{
    return this->m_pvTimingEvent;
}

CDemoTimingTask::CDemoTimingTask()
    : m_pAppContext(EC_NULL)
    , m_dwCpuIndex(0)
    , m_dwInstanceId(0)
    , m_nCycleTimeNsec(1000)
    , m_nOriginalCycleTimeNsec(1000)
    , m_bShutdown(EC_FALSE)
    , m_bIsRunning(EC_FALSE)
    , m_pvTimingThread(EC_NULL)
    , m_oTimingEvent()
{
}

CDemoTimingTask::CDemoTimingTask(_T_EC_DEMO_APP_CONTEXT& rAppContext)
    : m_pAppContext(&rAppContext)
    , m_dwCpuIndex(m_pAppContext->AppParms.dwCpuIndex)
    , m_dwInstanceId(m_pAppContext->AppParms.dwMasterInstanceId)
    , m_nCycleTimeNsec(1000)
    , m_nOriginalCycleTimeNsec(1000)
    , m_bShutdown(EC_FALSE)
    , m_bIsRunning(EC_FALSE)
    , m_pvTimingThread(EC_NULL)
    , m_oTimingEvent()
{
    if (this->m_pAppContext)
    {
        this->m_pAppContext->pTimingTaskContext = this;
    }
}

EC_T_DWORD CDemoTimingTask::CreateTimingEvent()
{
    EC_T_DWORD dwRet = this->m_oTimingEvent.Create();
    if (m_pAppContext != EC_NULL)
    {
        m_pAppContext->pvJobTaskEvent = this->GetRawTimingEvent();
    }
    return dwRet;
}

EC_T_DWORD CDemoTimingTask::SetTimingEvent()
{
    return this->m_oTimingEvent.Set();
}

EC_T_VOID  CDemoTimingTask::DeleteTimingEvent()
{
    this->m_oTimingEvent.Delete();
    if (m_pAppContext != EC_NULL)
    {
        m_pAppContext->pvJobTaskEvent = EC_NULL;
    }
}

EC_T_VOID* CDemoTimingTask::GetRawTimingEvent()
{
    return this->m_oTimingEvent.GetRaw();
}

EC_T_DWORD CDemoTimingTask::StartTimingTask(EC_T_INT _nCycleTimeNsec)
{
    this->m_nCycleTimeNsec = EC_MAX(_nCycleTimeNsec, 10);
    this->m_nOriginalCycleTimeNsec = this->m_nCycleTimeNsec;
    this->m_bShutdown = EC_FALSE;
    this->m_bIsRunning = EC_FALSE;

    EC_T_DWORD ret = this->CreateTimingEvent();
    if (ret != EC_E_NOERROR)
    {
        return ret;
    }

    ret = this->CreateThread();
    if (ret != EC_E_NOERROR)
    {
        return ret;
    }

    while (!this->m_bIsRunning)
    {
        OsSleep(1);
    }

    return EC_E_NOERROR;
}

EC_T_DWORD CDemoTimingTask::AdjustCycleTime(EC_T_INT nAdjustPermil)
{
    /* modify existing sleep cycle duration */
    if (this->m_bIsRunning)
    {
        EC_T_INT nAdjustment = (this->m_nOriginalCycleTimeNsec * nAdjustPermil) / 1000;
        this->m_nCycleTimeNsec = this->m_nOriginalCycleTimeNsec + nAdjustment;
    }

    return EC_E_NOERROR;
}

EC_T_DWORD CDemoTimingTask::StopTimingTask()
{
    if (!this->m_bIsRunning)
    {
        return EC_E_INVALIDSTATE;
    }

    this->m_bShutdown = EC_TRUE;
    while (this->m_bIsRunning)
    {
        OsSleep(1);
    }

    EC_T_DWORD ret = this->DeleteThread();
    if (ret != EC_E_NOERROR)
    {
        return ret;
    }
    
    this->DeleteTimingEvent();

    return EC_E_NOERROR;
}

CDemoTimingTask::~CDemoTimingTask()
{
    this->StopTimingTask();
    if (this->m_pAppContext)
    {
        this->m_pAppContext->pTimingTaskContext = EC_NULL;
    }
}

EC_T_DWORD CDemoTimingTask::CreateThread()
{
    EC_T_CHAR   szThreadName[20];
    EC_T_CPUSET CpuSet;

    EC_CPUSET_ZERO(CpuSet);
    EC_CPUSET_SET(CpuSet, this->m_dwCpuIndex);
    OsSnprintf(szThreadName, sizeof(szThreadName) - 1, "tDemoTimingTask_%d", this->m_dwInstanceId);

    this->m_pvTimingThread = OsCreateThread(szThreadName,
                                            (EC_PF_THREADENTRY)CDemoTimingTask::TimingTaskWrapper,
                                            CpuSet,
                                            TIMER_THREAD_PRIO,
                                            TIMER_THREAD_STACKSIZE,
                                            this);
    return this->m_pvTimingThread != EC_NULL ? EC_E_NOERROR : EC_E_ERROR;
}

EC_T_VOID CDemoTimingTask::TimingTaskWrapper(CDemoTimingTask* pDemoTimingTask)
{
    pDemoTimingTask->TimingTask();
}

EC_T_VOID CDemoTimingTask::TimingTask()
{
    /* timing task started */
    this->m_bIsRunning = EC_TRUE;

    /* periodically generate events as long as the application runs */
    while (!this->m_bShutdown)
    {
        /* wait for next cycle
           (no cycle below 1ms, sleep times of 0 on realtime operating systems can lead to a freez of the system) */
        OsSleep(EC_MAX(this->m_nCycleTimeNsec / 1000000, 1));

        /* trigger jobtask */
        this->SetTimingEvent();
    }
    this->m_bIsRunning = EC_FALSE;
}

EC_T_DWORD CDemoTimingTask::DeleteThread()
{
    if (this->m_pvTimingThread != EC_NULL)
    {
        OsDeleteThreadHandle(this->m_pvTimingThread);
        this->m_pvTimingThread = EC_NULL;
    }
    return EC_E_NOERROR;
}

EC_T_DWORD CDemoTimingTask::GetTimeElapsedSinceCycleStart(EC_T_VOID* pvContext, EC_T_DWORD* pdwTimeElapsedSinceCycleStartInNsec)
{
    EC_UNREFPARM(pvContext);
    EC_UNREFPARM(pdwTimeElapsedSinceCycleStartInNsec);
    return EC_E_NOTSUPPORTED;
}

EC_T_DWORD CDemoTimingTask::AdjustCycleTime(EC_T_VOID* pvContext, EC_T_INT nAdjustPermil)
{
    //Implemented as an example starting point. But will have no effect on tDemoTimingTask as OsSleep resolution is too less
    //and the EcatDrv (Windows) is just initialized once and not adjusted afterwards
    return ((CDemoTimingTask*)pvContext)->AdjustCycleTime(nAdjustPermil);
}

EC_T_DWORD CDemoTimingTask::GetHostTime(EC_T_VOID* pvContext, EC_T_UINT64* pnActualHostTimeInNsec)
{
    EC_UNREFPARM(pvContext);
    return OsSystemTimeGet(pnActualHostTimeInNsec);
}

EC_T_BOOL IsLinkLayerTimingSet(EC_T_LINK_PARMS* pLinkParms, EC_T_LINKLAYER_TIMING eLinkLayerTiming)
{
    return pLinkParms && pLinkParms->oLinkLayerTimingTask.eLinkLayerTiming == eLinkLayerTiming;
}

CDemoLinkLayerTimingTask::CDemoLinkLayerTimingTask(_T_EC_DEMO_APP_CONTEXT& rAppContext)
    : TBaseClass(rAppContext)
    , m_nLinkLayerIndex(IsLinkLayerTimingSet(m_pAppContext->AppParms.apLinkParms[0], eLinkLayerTiming_TTS) ? 0 :
                        IsLinkLayerTimingSet(m_pAppContext->AppParms.apLinkParms[1], eLinkLayerTiming_TTS) ? 1 :
                        IsLinkLayerTimingSet(m_pAppContext->AppParms.apLinkParms[0], eLinkLayerTiming_TMR) ? 0 :
                        IsLinkLayerTimingSet(m_pAppContext->AppParms.apLinkParms[1], eLinkLayerTiming_TMR) ? 1 : 0xFFFF)
{
    if (m_nLinkLayerIndex != 0xFFFF)
    {
        m_pAppContext->AppParms.apLinkParms[m_nLinkLayerIndex]->oLinkLayerTimingTask.pfnStartCycle       = CDemoLinkLayerTimingTask::StartCycle;
        m_pAppContext->AppParms.apLinkParms[m_nLinkLayerIndex]->oLinkLayerTimingTask.pvStartCycleContext = this;
        this->CreateTimingEvent();
        //If 0 will be set by the stack by also calling OsSystemTimeGet. But more precise as closer to the start of the timing task and after LinkLayer initialization.
        //OsSystemTimeGet(&m_rAppContext.AppParms.apLinkParms[m_nLinkLayerIndex]->oLinkLayerTimingTask.nSystemTime);
    }
}

EC_T_DWORD CDemoLinkLayerTimingTask::StartTimingTask(EC_T_INT)
{
    return EC_E_NOTSUPPORTED;
}

EC_T_DWORD CDemoLinkLayerTimingTask::AdjustCycleTime(EC_T_INT)
{
    return EC_E_NOTSUPPORTED;
}

EC_T_DWORD CDemoLinkLayerTimingTask::StopTimingTask()
{
    return EC_E_NOTSUPPORTED;
}

CDemoLinkLayerTimingTask::~CDemoLinkLayerTimingTask()
{
    if (m_nLinkLayerIndex != 0xFFFF)
    {
        m_pAppContext->AppParms.apLinkParms[m_nLinkLayerIndex]->oLinkLayerTimingTask.pfnStartCycle = EC_NULL;
        m_pAppContext->AppParms.apLinkParms[m_nLinkLayerIndex]->oLinkLayerTimingTask.pvStartCycleContext = EC_NULL;
        this->DeleteTimingEvent();
    }
}

void CDemoLinkLayerTimingTask::StartCycle(EC_T_VOID* pvStartCycleContext)
{
    ((CDemoLinkLayerTimingTask*)pvStartCycleContext)->SetTimingEvent();
}

