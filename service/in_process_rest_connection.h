/** in_process_rest_connection.h                                  -*- C++ -*-
 *
    Jeremy Barnes, January 2015
    Copyright (c) 2015 Datacratic Inc.  All rights reserved.

*/

#pragma once

#include "http_rest_service.h"

namespace Datacratic {

/*****************************************************************************/
/* IN PROCESS HTTP REST CONNECTION                                           */
/*****************************************************************************/

struct InProcessRestConnection: public HttpRestConnection {

    InProcessRestConnection();

    virtual ~InProcessRestConnection();

    using RestConnection::sendResponse;

    /** Send the given response back on the connection. */
    virtual void sendResponse(int responseCode,
                              const std::string & response,
                              const std::string & contentType);

    /** Send the given response back on the connection. */
    virtual void
    sendResponse(int responseCode,
                 const Json::Value & response,
                 const std::string & contentType = "application/json");

    virtual void sendRedirect(int responseCode,
                              const std::string & location);

    /** Send an HTTP-only response with the given headers.  If it's not
        an HTTP connection, this will fail.
    */
    virtual void sendHttpResponse(int responseCode,
                                  const std::string & response,
                                  const std::string & contentType,
                                  const RestParams & headers);

    virtual void
    sendHttpResponseHeader(int responseCode,
                           const std::string & contentType,
                           ssize_t contentLength,
                           const RestParams & headers = RestParams());

    virtual void sendPayload(const std::string & payload);

    virtual void finishResponse();

    /** Send the given error string back on the connection. */
    virtual void sendErrorResponse(int responseCode,
                                   const std::string & error,
                                   const std::string & contentType);

    using RestConnection::sendErrorResponse;

    virtual void sendErrorResponse(int responseCode, const Json::Value & error);
    virtual bool responseSent() const;
    virtual bool isConnected() const;

    int responseCode;
    std::string contentType;
    RestParams headers;
    std::string response;

    virtual std::shared_ptr<RestConnection>
    capture(std::function<void ()> onDisconnect);

    virtual std::shared_ptr<RestConnection>
    captureInConnection(std::shared_ptr<void> toCapture);

};

}

