/* sns.h                                                           -*- C++ -*-
   Jeremy Barnes, 12 May 2013
   Copyright (c) 2013 Datacratic Inc.  All rights reserved.

   Client for Amazon's Simple Notification Service.
*/

#pragma once

#include "s3.h"

namespace Datacratic {


/*****************************************************************************/
/* SNS API                                                                   */
/*****************************************************************************/
 
struct SnsApi {

    std::string accessKeyId;
    std::string accessKey;
    std::string serviceUri;

    HttpRestProxy proxy;

    SnsApi();

    /** Set up the API to called with the given credentials. */
    SnsApi(const std::string & accessKeyId,
           const std::string & accessKey,
           const std::string & serviceUri = "http://sns.us-east-1.amazonaws.com/");

    /** Set up the API to called with the given credentials. */
    void init(const std::string & accessKeyId,
              const std::string & accessKey,
              const std::string & serviceUri = "http://sns.us-east-1.amazonaws.com/");

    /** Publish a message to a given SNS topic.  Returns the Message ID assigned
        by Amazon.

        By default, this will retry a failure 3 times before throwing an
        exceptoin.

        \param topicArn      The Amazon topic to send the message to.
        \param message       The message to be sent
        \param timeout       The timeout after which to retry
        \param subject       The optional subject to give to the message
    */
    std::string
    publish(const std::string & topicArn,
            const std::string & message,
            int timeout = 10,
            const std::string & subject = "");
};


} // namespace Datacratic
