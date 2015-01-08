/* event_service.h                                                 -*- C++ -*-
   Jeremy Barnes, 12 December 2014
   Copyright (c) 2014 Datacratic Inc.  All rights reserved.

   Service for high frequency logging of events.
*/

#pragma once

#include "soa/service/stats_events.h"
#include <map>
#include <string>


namespace Datacratic {

class MultiAggregator;
class CarbonConnector;
class ServiceProxies;

/*****************************************************************************/
/* EVENT SERVICE                                                             */
/*****************************************************************************/

struct EventService {
    virtual ~EventService()
    {
    }
    
    virtual void onEvent(const std::string & name,
                         const char * event,
                         EventType type,
                         float value) = 0;

    virtual void dump(std::ostream & stream) const
    {
    }

    /** Dump the content
    */
    std::map<std::string, double> get(std::ostream & output) const;
};

/*****************************************************************************/
/* NULL EVENT SERVICE                                                        */
/*****************************************************************************/

struct NullEventService : public EventService {

    NullEventService();
    ~NullEventService();
    
    virtual void onEvent(const std::string & name,
                         const char * event,
                         EventType type,
                         float value);

    virtual void dump(std::ostream & stream) const;

    std::unique_ptr<MultiAggregator> stats;
};


/*****************************************************************************/
/* CARBON EVENT SERVICE                                                      */
/*****************************************************************************/

struct CarbonEventService : public EventService {

    CarbonEventService(std::shared_ptr<CarbonConnector> conn);
    CarbonEventService(const std::string & connection,
                       const std::string & prefix = "",
                       double dumpInterval = 1.0);
    CarbonEventService(const std::vector<std::string> & connections,
                       const std::string & prefix = "",
                       double dumpInterval = 1.0);

    virtual void onEvent(const std::string & name,
                         const char * event,
                         EventType type,
                         float value);

    std::shared_ptr<CarbonConnector> connector;
};


/*****************************************************************************/
/* EVENT RECORDER                                                            */
/*****************************************************************************/

/** Bridge class to an event recorder. */

struct EventRecorder {

    EventRecorder(const std::string & eventPrefix,
                  const std::shared_ptr<EventService> & events);

    EventRecorder(const std::string & eventPrefix,
                  const std::shared_ptr<ServiceProxies> & services);


    /*************************************************************************/
    /* EVENT RECORDING                                                       */
    /*************************************************************************/

    /** Notify that an event has happened.  Fields are:
        eventNum:  an ID for the event;
        eventName: the name of the event;
        eventType: the type of the event.  Default is ET_COUNT;
        value:     the value of the event (quantity being measured).
                   Default is 1.0;
        units:     the units of the event (eg, ms).  Default is unitless.
    */
    void recordEvent(const char * eventName,
                     EventType type = ET_COUNT,
                     float value = 1.0) const;

    void recordEventFmt(EventType type,
                        float value,
                        const char * fmt, ...) const JML_FORMAT_STRING(4, 5);

    template<typename... Args>
    void recordHit(const std::string & event, Args... args) const
    {
        return recordEventFmt(ET_HIT, 1.0, event.c_str(),
                              ML::forwardForPrintf(args)...);
    }

    template<typename... Args>
    JML_ALWAYS_INLINE
    void recordHit(const char * event, Args... args) const
    {
        return recordEventFmt(ET_HIT, 1.0, event,
                              ML::forwardForPrintf(args)...);
    }

    void recordHit(const char * event) const
    {
        recordEvent(event, ET_HIT);
    }

    void recordHit(const std::string & event) const
    {
        recordEvent(event.c_str(), ET_HIT);
    }

    template<typename... Args>
    void recordCount(float count, const std::string & event, Args... args) const
    {
        return recordEventFmt(ET_COUNT, count, event.c_str(),
                              ML::forwardForPrintf(args)...);
    }
    
    template<typename... Args>
    JML_ALWAYS_INLINE
    void recordCount(float count, const char * event, Args... args) const
    {
        return recordEventFmt(ET_COUNT, count, event,
                              ML::forwardForPrintf(args)...);
    }

    void recordCount(float count, const char * event) const
    {
        recordEvent(event, ET_COUNT, count);
    }

    void recordCount(float count, const std::string & event) const
    {
        recordEvent(event.c_str(), ET_COUNT, count);
    }

    template<typename... Args>
    void recordOutcome(float outcome, const std::string & event, Args... args) const
    {
        return recordEventmt(ET_OUTCOME, outcome, event.c_str(),
                             ML::forwardForPrintf(args)...);
    }
    
    template<typename... Args>
    void recordOutcome(float outcome, const char * event, Args... args) const
    {
        return recordEventFmt(ET_OUTCOME, outcome, event,
                              ML::forwardForPrintf(args)...);
    }

    void recordOutcome(float outcome, const char * event) const
    {
        recordEvent(event, ET_OUTCOME, outcome);
    }

    void recordOutcome(float outcome, const std::string & event) const
    {
        recordEvent(event.c_str(), ET_OUTCOME, outcome);
    }
    
    template<typename... Args>
    void recordLevel(float level, const std::string & event, Args... args) const
    {
        return recordEventmt(ET_LEVEL, level, event.c_str(),
                             ML::forwardForPrintf(args)...);
    }
    
    template<typename... Args>
    void recordLevel(float level, const char * event, Args... args) const
    {
        return recordEventFmt(ET_LEVEL, level, event,
                              ML::forwardForPrintf(args)...);
    }

    void recordLevel(float level, const char * event) const
    {
        recordEvent(event, ET_LEVEL, level);
    }

    void recordLevel(float level, const std::string & event) const
    {
        recordEvent(event.c_str(), ET_LEVEL, level);
    }

    template<typename... Args>
    void recordStableLevel(float level, const std::string & event, Args... args) const
    {
        return recordEventmt(ET_STABLE_LEVEL, level, event.c_str(),
                             ML::forwardForPrintf(args)...);
    }

    template<typename... Args>
    void recordStableLevel(float level, const char * event, Args... args) const
    {
        return recordEventFmt(ET_STABLE_LEVEL, level, event,
                              ML::forwardForPrintf(args)...);
    }

    void recordStableLevel(float level, const char * event) const
    {
        recordEvent(event, ET_STABLE_LEVEL, level);
    }

    void recordStableLevel(float level, const std::string & event) const
    {
        recordEvent(event.c_str(), ET_STABLE_LEVEL, level);
    }

protected:
    std::string eventPrefix_;
    std::shared_ptr<EventService> events_;
    std::shared_ptr<ServiceProxies> services_;
};


} // namespace Datacratic
