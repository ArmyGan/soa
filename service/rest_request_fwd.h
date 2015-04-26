/** rest_request_fwd.h                                             -*- C++ -*-
    Jeremy Barnes, 26 April 2015
    Copyright (c) 2015 Datacratic Inc.  All rights reserved.

    Forward definitions for REST requests.
*/

#pragma once

namespace Datacratic {

enum RestRequestMatchResult {
    MR_NO,     ///< Didn't match but can continue
    MR_YES,    ///< Did match
    MR_ERROR,  ///< Error
    MR_ASYNC   ///< Handled, but asynchronously
};    

struct ServicePeer;
struct RestConnection;
struct RestRequest;
struct RestRequestParsingContext;


} // namespace Datacratic

