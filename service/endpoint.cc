/* endpoint.cc
   Jeremy Barnes, 21 February 2011
   Copyright (c) 2011 Datacratic.  All rights reserved.

*/

#include <sys/timerfd.h>

#include "soa/service//endpoint.h"

#include "soa/service//http_endpoint.h"
#include "jml/arch/cmp_xchg.h"
#include "jml/arch/atomic_ops.h"
#include "jml/arch/format.h"
#include "jml/arch/exception.h"
#include "jml/arch/demangle.h"
#include "jml/arch/backtrace.h"
#include "jml/arch/timers.h"
#include "jml/arch/futex.h"
#include "jml/utils/set_utils.h"
#include "jml/utils/guard.h"
#include "jml/utils/vector_utils.h"
#include "jml/utils/smart_ptr_utils.h"
#include "jml/utils/exc_assert.h"
#include "jml/arch/rt.h"
#include <sys/prctl.h>
#include <sys/epoll.h>
#include <poll.h>


using namespace std;
using namespace ML;


namespace Datacratic {

/*****************************************************************************/
/* ENDPOINT BASE                                                             */
/*****************************************************************************/

EndpointBase::
EndpointBase(const std::string & name)
    : idle(1), modifyIdle(true),
      name_(name),
      threadsActive_(0),
      numTransports(0), shutdown_(false), disallowTimers_(false)
{
    Epoller::init(16384);
    auto wakeupData = make_shared<EpollData>(EpollData::EpollDataType::WAKEUP,
                                             wakeup.fd());
    epollDataSet.insert(wakeupData);
    Epoller::addFd(wakeupData->fd, wakeupData.get());
    Epoller::handleEvent = [&] (epoll_event & event) {
        return this->handleEpollEvent(event);
    };
}

EndpointBase::
~EndpointBase()
{
    shutdown();
}

void
EndpointBase::
addPeriodic(double timePeriodSeconds, OnTimer toRun)
{
    if (!toRun)
        throw ML::Exception("'toRun' cannot be nil");

    int timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerFd == -1)
        throw ML::Exception(errno, "timerfd_create");

    itimerspec spec;
    
    int res = clock_gettime(CLOCK_MONOTONIC, &spec.it_value);
    if (res == -1)
        throw ML::Exception(errno, "clock_gettime");
    uint64_t seconds, nanoseconds;
    seconds = timePeriodSeconds;
    nanoseconds = (timePeriodSeconds - seconds) * 1000000000;

    spec.it_interval.tv_sec = spec.it_value.tv_sec = seconds;
    spec.it_interval.tv_nsec = spec.it_value.tv_nsec = nanoseconds;

    res = timerfd_settime(timerFd, 0, &spec, 0);
    if (res == -1)
        throw ML::Exception(errno, "timerfd_settime");

    auto timerData = make_shared<EpollData>(EpollData::EpollDataType::TIMER,
                                            timerFd);
    timerData->onTimer = toRun;
    startPolling(timerData);
}

void
EndpointBase::
spinup(int num_threads, bool synchronous)
{
    shutdown_ = false;

    if (eventThreads)
        throw Exception("spinup with threads already up");
    eventThreads.reset(new boost::thread_group());

    threadsActive_ = 0;

    totalSleepTime.resize(num_threads, 0.0);

    for (unsigned i = 0;  i < num_threads;  ++i) {
        boost::thread * thread
            = eventThreads->create_thread
            ([=] ()
             {
                 this->runEventThread(i, num_threads);
             });
        eventThreadList.push_back(thread);
    }

    if (synchronous) {
        for (;;) {
            int oldValue = threadsActive_;
            if (oldValue >= num_threads) break;
            //cerr << "threadsActive_ " << threadsActive_
            //     << " of " << num_threads << endl;
            futex_wait(threadsActive_, oldValue);
            //ML::sleep(0.001);
        }
    }
}

void
EndpointBase::
makeRealTime(int priority)
{
    for (unsigned i = 0;  i < eventThreadList.size();  ++i)
        makeThreadRealTime(*eventThreadList[i], priority);
}

void
EndpointBase::
shutdown()
{
    //cerr << "Endpoint shutdown" << endl;
    //cerr << "numTransports = " << numTransports << endl;

    /* we pin all EpollDataSet instances to avoid freeing them whilst handling
       messages */
    EpollDataSet dataSetCopy = epollDataSet;
    
    {
        Guard guard(lock);
        //cerr << "sending shutdown to " << transportMapping.size()
        //<< " transports" << endl;

        for (const auto & it: transportMapping) {
            auto transport = it.first.get();
            //cerr << "shutting down transport " << transport->status() << endl;
            transport->doAsync([=] ()
                               {
                                   //cerr << "killing transport " << transport
                                   //     << endl;
                                   transport->closeWhenHandlerFinished();
                               },
                               "killtransport");
        }
    }

    disallowTimers_ = true;
    ML::memory_barrier();
    {
        /* we remove timer infos separately, as transport infos will be
           removed via notifyCloseTransport */
        for (const auto & it: dataSetCopy) {
            if (it->fdType == EpollData::EpollDataType::TIMER) {
                stopPolling(it);
            }
        }
    }

    //cerr << "eventThreads = " << eventThreads.get() << endl;
    //cerr << "eventThreadList.size() = " << eventThreadList.size() << endl;

    //cerr << "numTransports = " << numTransports << endl;

    sleepUntilIdle();

    //cerr << "idle" << endl;

    while (numTransports != 0) {
        //cerr << "shutdown " << this << ": numTransports = "
        //     << numTransports << endl;
        int oldValue = numTransports;
        ML::futex_wait(numTransports, oldValue);
    }

    //cerr << "numTransports = " << numTransports << endl;

    shutdown_ = true;
    ML::memory_barrier();
    wakeup.signal();

    while (threadsActive_ > 0) {
        int oldValue = threadsActive_;
        ML::futex_wait(threadsActive_, oldValue);
    }

    {
        /* we can now close the timer fds as we now that they will no longer
           be listened to */
        MutexGuard guard(dataSetLock);
        for (const auto & it: dataSetCopy) {
            if (it->fdType == EpollData::EpollDataType::TIMER) {
                ::close(it->fd);
            }
        }
    }

    if (eventThreads) {
        eventThreads->join_all();
        eventThreads.reset();
    }
    eventThreadList.clear();

    // Now undo the signal
    wakeup.read();

}

void
EndpointBase::
useThisThread()
{
    runEventThread(-1, -1);
}

void
EndpointBase::
notifyNewTransport(const std::shared_ptr<TransportBase> & transport)
{
    Guard guard(lock);

    //cerr << "new transport " << transport << endl;

    if (transportMapping.count(transport))
        throw ML::Exception("active set already contains connection");
    auto epollData
        = make_shared<EpollData>(EpollData::EpollDataType::TRANSPORT,
                                 transport->epollFd_);
    epollData->transport = transport;
    transportMapping.insert({transport, epollData});

    int fd = transport->getHandle();
    if (fd < 0)
        throw Exception("notifyNewTransport: fd %d out of range");

    startPolling(epollData);

    ML::atomic_inc(numTransports);
    if (numTransports == 1 && modifyIdle)
        idle.acquire();
    futex_wake(numTransports);

    int & ntr = numTransportsByHost[transport->getPeerName()];
    ++ntr;

    //cerr << "host " << transport->getPeerName() << " has "
    //     << ntr << " connections" << endl;


    if (onTransportOpen)
        onTransportOpen(transport.get());
}

void
EndpointBase::
startPolling(const shared_ptr<EpollData> & epollData)
{
    MutexGuard guard(dataSetLock);
    auto inserted = epollDataSet.insert(epollData);
    if (!inserted.second)
        throw ML::Exception("epollData already present");
    addFdOneShot(epollData->fd, epollData.get());
}

void
EndpointBase::
stopPolling(const shared_ptr<EpollData> & epollData)
{ 
    removeFd(epollData->fd);
    MutexGuard guard(dataSetLock);
    epollDataSet.erase(epollData);
}

void
EndpointBase::
restartPolling(EpollData * epollDataPtr)
{
    restartFdOneShot(epollDataPtr->fd, epollDataPtr);
}

void
EndpointBase::
notifyCloseTransport(const std::shared_ptr<TransportBase> & transport)
{
#if 0
    cerr << "closed transport " << transport << " with fd "
         << transport->getHandle() << " with " << transport.use_count()
         << " references" << " and " << transport->hasAsync() << " async"
         << endl;
#endif

    if (onTransportClose)
        onTransportClose(transport.get());

    Guard guard(lock);
    if (!transportMapping.count(transport)) {
        cerr << "closed transport " << transport << " with fd "
             << transport->getHandle() << " with " << transport.use_count()
             << " references" << " and " << transport->hasAsync() << " async"
             << endl;
        cerr << "activities: " << endl;
        transport->activities.dump();
        cerr << endl << endl;

        throw ML::Exception("transportMapping didn't contain connection");
    }

    auto epollData = transportMapping.at(transport);
    stopPolling(epollData);
    transportMapping.erase(transport); 
    
    transport->zombie_ = true;
    transport->closePeer();

    int & ntr = numTransportsByHost[transport->getPeerName()];
    ML::atomic_dec(numTransports);
    futex_wake(numTransports);
    --ntr;
    if (ntr <= 0)
        numTransportsByHost.erase(transport->getPeerName());
    if (numTransports == 0 && modifyIdle)
        idle.release();
}

void
EndpointBase::
notifyRecycleTransport(const std::shared_ptr<TransportBase> & transport)
{
    notifyCloseTransport(transport);
}

void
EndpointBase::
sleepUntilIdle() const
{
    for (;;) {
        //cerr << "sleepUntilIdle " << this << ": numTransports = "
        //     << numTransports << endl;
        ACE_Time_Value time(0, 100000);
        time += ACE_OS::gettimeofday();
        int res = idle.acquire(time);
        if (res != -1) {
            idle.release();
            return;
        }

        Guard guard(lock);
        cerr << transportMapping.size() << " transports" << endl;

        for (auto & it: transportMapping) {
            auto transport = it.first;
            cerr << "transport " << transport->status() << endl;
        }

        dumpState();
    }
}

void
EndpointBase::
dumpState() const
{
    Guard guard(lock);
    cerr << "----------------------------------------------" << endl;
    cerr << "Endpoint of type " << type_name(*this)
         << " with " << numTransports << " transports"
         << endl;

}

int
EndpointBase::
numConnections() const
{
    return numTransports;
}

std::map<std::string, int>
EndpointBase::
numConnectionsByHost() const
{
    Guard guard(lock);
    return numTransportsByHost;
}

/** Handle a single ePoll event */
bool
EndpointBase::
handleEpollEvent(epoll_event & event)
{
    bool debug = false;

    if (debug) {
        cerr << "handleEvent" << endl;
        int mask = event.events;
                
        cerr << "events " 
             << (mask & EPOLLIN ? "I" : "")
             << (mask & EPOLLOUT ? "O" : "")
             << (mask & EPOLLPRI ? "P" : "")
             << (mask & EPOLLERR ? "E" : "")
             << (mask & EPOLLHUP ? "H" : "")
             << (mask & EPOLLRDHUP ? "R" : "")
             << endl;
    }

    EpollData * epollDataPtr = reinterpret_cast<EpollData *>(event.data.ptr);
    switch (epollDataPtr->fdType) {
    case EpollData::EpollDataType::TRANSPORT: {
        shared_ptr<TransportBase> transport = epollDataPtr->transport;
        handleTransportEvent(transport);
        if (!transport->isZombie()) {
            this->restartPolling(epollDataPtr);
        }
        break;
    }
    case EpollData::EpollDataType::TIMER: {
        handleTimerEvent(epollDataPtr->fd, epollDataPtr->onTimer);
        if (!disallowTimers_) {
            this->restartPolling(epollDataPtr);
        }
        break;
    }
    case EpollData::EpollDataType::WAKEUP:
        // wakeup for shutdown
        return true;
    default:
        throw ML::Exception("unrecognized fd type");
    }


    return false;
}

void
EndpointBase::
handleTransportEvent(const shared_ptr<TransportBase> & transport)
{
    bool debug = false;

    //cerr << "transport_ = " << transport_.get() << endl; 

    if (debug)
        cerr << "transport status = " << transport->status() << endl;

    transport->handleEvents();
}

void
EndpointBase::
handleTimerEvent(int fd, OnTimer toRun)
{
    uint64_t numWakeups = 0;
    for (;;) {
        int res = ::read(fd, &numWakeups, 8);
        if (res == -1 && errno == EINTR) continue;
        if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (res == -1) {
            throw ML::Exception(errno, "timerfd read");
        }
        else if (res != 8)
            throw ML::Exception("timerfd read: wrong number of bytes: %d",
                                res);
        toRun(numWakeups);
        break;
    }
}

void
EndpointBase::
runEventThread(int threadNum, int numThreads)
{
    prctl(PR_SET_NAME,"EptCtrl",0,0,0);

    ML::Duty_Cycle_Timer duty;

    ML::atomic_inc(threadsActive_);
    futex_wake(threadsActive_);

    Epoller::OnEvent beforeSleep = [&] ()
        {
            duty.notifyBeforeSleep();
        };

    Epoller::OnEvent afterSleep = [&] ()
        {
            duty.notifyAfterSleep();
            totalSleepTime[threadNum] += duty.afterSleep - duty.beforeSleep;
        };

    // Null scheduler - It's CPU heavy but minimizes the variance on the network
    // observed network latency.
    while (!shutdown_)
        handleEvents(0, 4, handleEvent, beforeSleep, afterSleep);

    cerr << "thread shutting down" << endl;

    ML::atomic_dec(threadsActive_);
    futex_wake(threadsActive_);
}

} // namespace Datacratic
