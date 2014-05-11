#include <errno.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include "jml/arch/cmp_xchg.h"
#include "jml/arch/timers.h"
#include "jml/arch/exception.h"
#include "jml/utils/string_functions.h"

#include "soa/types/url.h"
#include "soa/service/message_loop.h"
#include "soa/service/http_header.h"

#include "http_client.h"


using namespace std;
using namespace Datacratic;


/* HTTP REQUEST */

void
HttpRequest::
makeRequestStr()
    noexcept
{
    Url url(url_);
    requestStr_.reserve(10000);
    requestStr_ = verb_ + " " + url.path();
    string query = url.query();
    if (query.size() > 0) {
        requestStr_ += "?" + query;
    }
    requestStr_ += " HTTP/1.1\r\n";

    requestStr_ += "Host: "+ url.host();
    int port = url.port();
    if (port > 0) {
        requestStr_ += ":" + to_string(port);
    }
    requestStr_ += "\r\nAccept: */*\r\n";
    for (const auto & header: headers_) {
        requestStr_ += header.first + ":" + header.second + "\r\n";
    }
    if (!content_.isVoid()) {
        requestStr_ += ("Content-Length: "
                        + to_string(content_.size()) + "\r\n");
        requestStr_ += "Content-Type: " + content_.contentType() + "\r\n";
    }
    requestStr_ += "\r\n";
}


/* HTTPCLIENTCALLBACKS */

#if 0
const string &
HttpClientCallbacks::
errorMessage(HttpClientError errorCode)
{
    static const string none = "No error";
    static const string unknown = "Unknown error";
    static const string hostNotFound = "Host not found";
    static const string couldNotConnect = "Could not connect";
    static const string timeout = "Request timed out";

    switch (errorCode) {
    case HttpClientError::SUCCESS:
        return none;
    case HttpClientError::UNKNOWN:
        return unknown;
    case HttpClientError::TIMEOUT:
        return timeout;
    case HttpClientError::HOST_UNKNOWN:
        return hostNotFound;
    case HttpClientError::COULD_NOT_CONNECT:
        return couldNotConnect;
    default:
        throw ML::Exception("invalid error code");
    };
}
#endif

void
HttpClientCallbacks::
onResponseStart(const HttpRequest & rq,
                const string & httpVersion, int code)
{
    if (onResponseStart_)
        onResponseStart_(rq, httpVersion, code);
}

void
HttpClientCallbacks::
onHeader(const HttpRequest & rq, const char * data, size_t size)
{
    if (onHeader_)
        onHeader_(rq, data, size);
}

void
HttpClientCallbacks::
onData(const HttpRequest & rq, const char * data, size_t size)
{
    if (onData_)
        onData_(rq, data, size);
}

void
HttpClientCallbacks::
onDone(const HttpRequest & rq, int errorCode)
{
    if (onDone_)
        onDone_(rq, errorCode);
}


/* HTTP RESPONSE PARSER */

void
HttpResponseParser::
clear()
    noexcept
{
    state_ = 0;
    buffer_.resize(0);
    remainingBody_ = 0;
}

void
HttpResponseParser::
feed(const char * bufferData)
{
    // cerr << "feed: /" + ML::hexify_string(string(bufferData)) + "/\n";
    feed(bufferData, strlen(bufferData));
}

void
HttpResponseParser::
feed(const char * bufferData, size_t bufferSize)
{
    const char * data;
    size_t dataSize;
    bool fromBuffer;

    // cerr << ("data: /"
    //          + ML::hexify_string(string(bufferData, bufferSize))
    //          + "/\n");

    if (buffer_.size() > 0) {
        buffer_.append(bufferData, bufferSize);
        data = buffer_.c_str();
        fromBuffer = true;
        dataSize = buffer_.size();
    }
    else {
        data = bufferData;
        fromBuffer = false;
        dataSize = bufferSize;
    }

    size_t ptr = 0;

    auto skipToChar = [&] (char c, bool throwOnEol) {
        while (ptr < dataSize) {
            if (data[ptr] == c)
                return true;
            else if (throwOnEol
                     && (data[ptr] == '\r' || data[ptr] == '\n')) {
                throw ML::Exception("unexpected end of line");
            }
            ptr++;
        }

        return false;
    };

    // cerr << ("state: " + to_string(state_)
    //          + "; dataSize: " + to_string(dataSize) + "\n");

    while (true) {
        if (ptr == dataSize) {
            if (fromBuffer) {
                buffer_.clear();
            }
            return;
        }
        if (state_ == 0) {
            // status line
            // HTTP/1.1 200 OK

            /* sizeof("HTTP/1.1 200 ") */
            if ((dataSize - ptr) < 16) {
                if (!fromBuffer) {
                    buffer_.assign(data + ptr, dataSize - ptr);
                }
                return;
            }

            if (::memcmp(data + ptr, "HTTP/", 5) != 0) {
                throw ML::Exception("version must start with 'HTTP/'");
            }
            ptr += 5;

            if (!skipToChar(' ', true)) {
                /* post-version ' ' not found even though size is sufficient */
                throw ML::Exception("version too long");
            }
            size_t versionEnd = ptr;

            ptr++;
            size_t codeStart = ptr;
            if (!skipToChar(' ', true)) {
                /* post-code ' ' not found even though size is sufficient */
                throw ML::Exception("code too long");
            }

            size_t codeEnd = ptr;
            int code = ML::antoi(data + codeStart, data + codeEnd);

            /* we skip the whole "reason" string */
            if (!skipToChar('\r', false)) {
                if (!fromBuffer) {
                    buffer_.assign(data + ptr, dataSize - ptr);
                }
                return;
            }
            ptr++;
            if (ptr == dataSize) {
                return;
            }
            if (data[ptr] != '\n') {
                throw ML::Exception("expected \\n");
            }
            ptr++;
            onResponseStart(string(data, versionEnd), code);
            state_ = 1;

            if (ptr == dataSize) {
                buffer_.clear();
                return;
            }
        }
        else if (state_ == 1) {
            while (data[ptr] != '\r') {
                size_t headerPtr = ptr;
                if (!skipToChar(':', true) || !skipToChar('\r', false)) {
                    if (headerPtr > 0 || !fromBuffer) {
                        buffer_.assign(data + headerPtr,
                                       dataSize - headerPtr);
                    }
                    return;
                }
                ptr++;
                if (ptr == dataSize) {
                    if (headerPtr > 0 || !fromBuffer) {
                        buffer_.assign(data + headerPtr,
                                       dataSize - headerPtr);
                    }
                    return;
                }
                if (data[ptr] != '\n') {
                    throw ML::Exception("expected \\n");
                }
                ptr++;
                handleHeader(data + headerPtr, ptr - headerPtr - 2);
                if (ptr == dataSize) {
                    // cerr << "returning\n";
                    if (fromBuffer) {
                        buffer_.clear();
                    }
                    return;
                }
            }
            if (ptr + 1 == dataSize) {
                buffer_.assign(data + ptr, dataSize - ptr);
                return;
            }
            ptr++;
            if (data[ptr] != '\n') {
                throw ML::Exception("expected \\n");
            }
            ptr++;

            if (remainingBody_ == 0) {
                onDone();
                state_ = 0;
            }
            else {
                state_ = 2;
            }

            if (dataSize == 0) {
                if (fromBuffer) {
                    buffer_.clear();
                }
                return;
            }
        }
        else if (state_ == 2) {
            uint64_t chunkSize = min(dataSize - ptr, remainingBody_);
            // cerr << "toSend: " + to_string(chunkSize) + "\n";
            // cerr << "received body: /" + string(data, chunkSize) + "/\n";
            onData(data + ptr, chunkSize);
            ptr += chunkSize;
            remainingBody_ -= chunkSize;
            if (remainingBody_ == 0) {
                onDone();
                state_ = 0;
            }
        }
    }
}

void
HttpResponseParser::
handleHeader(const char * data, size_t dataSize)
{
    size_t ptr(0);

    auto skipToChar = [&] (char c) {
        while (ptr < dataSize) {
            if (data[ptr] == c)
                return true;
            ptr++;
        }

        return false;
    };
    auto skipChar = [&] (char c) {
        while (ptr < dataSize && data[ptr] == c) {
            ptr++;
        }
    };
    auto matchString = [&] (const char * testString, size_t len) {
        bool result;
        if (dataSize >= (ptr + len)
            && ::strncasecmp(data + ptr, testString, len) == 0) {
            ptr += len;
            result = true;
        }
        else {
            result = false;
        }
        return result;
    };

    if (matchString("Content-Length", 14)) {
        skipChar(' ');
        skipToChar(':');
        ptr++;
        skipChar(' ');
        remainingBody_ = ML::antoi(data + ptr, data + dataSize);
    }

    onHeader(data, dataSize);
}

/* HTTP CONNECTION */

HttpConnection::
HttpConnection()
    : responseState_(IDLE)
{
    // cerr << "HttpConnection(): " << this << "\n";

    parser_.onResponseStart = [&] (const std::string & httpVersion,
                                   int code) {
        this->onParserResponseStart(httpVersion, code);
    };
    parser_.onHeader = [&] (const char * data, size_t size) {
        this->onParserHeader(data, size);
    };
    parser_.onData = [&] (const char * data, size_t size) {
        this->onParserData(data, size);
    };
    parser_.onDone = [&] () {
        this->onParserDone();
    };
}

HttpConnection::
~HttpConnection()
{
    // cerr << "~HttpConnection: " << this << "\n";
}

void
HttpConnection::
clear()
{
    responseState_ = IDLE;
    request_.clear();
    uploadOffset_ = 0;
}

void
HttpConnection::
perform(HttpRequest && request)
{
    // cerr << "perform: " << this << endl;

    if (responseState_ != IDLE) {
        ::fprintf(stderr, "cannot process a request when state is not idle");
        abort();
    }

    request_ = move(request);

    responseState_ = HEADERS;
    if (canSendMessages()) {
        write(request_.requestStr());
    }
    else {
        connect();
    }
}

void
HttpConnection::
onConnectionResult(ConnectionResult result, const vector<string> & msgs)
{
    // cerr << " onConnectionResult: " << this << "  " + to_string(result) + "\n";
    if (result == ConnectionResult::SUCCESS) {
        write(request_.requestStr());
    }
    else {
        cerr << " failure with result: "  + to_string(result) + "\n";
        handleEndOfRq(result);
    }
}

void
HttpConnection::
onWriteResult(int error, const string & written, size_t writtenSize)
{
    if (error == 0) {
        const MimeContent content = request_.content();
        if (responseState_ == HEADERS) {
            if (content.size() > 0) {
                responseState_ = BODY;
                uploadOffset_ = 0;
            }
            else {
                responseState_ = IDLE;
            }
        }
        else if (responseState_ == BODY) {
            uploadOffset_ += writtenSize;
        }
        else if (responseState_ != BODY) {
            throw ML::Exception("invalid state");
        }
        if (responseState_ == BODY) {
            uint64_t remaining = content.size() - uploadOffset_;
            uint64_t chunkSize = min(remaining, HttpConnection::sendSize);
            if (chunkSize == 0) {
                responseState_ = IDLE;
            }
            else {
                write(content.data() + uploadOffset_, chunkSize);
            }
        }
    }
    else {
        throw ML::Exception("unhandled error");
    }
}

void
HttpConnection::
onReceivedData(const char * data, size_t size)
{
    // cerr << "onReceivedData: " + string(data, size) + "\n";
    parser_.feed(data, size);
}

void
HttpConnection::
onException(const exception_ptr & excPtr)
{
    cerr << "http client received exception\n";
    abort();
}

void
HttpConnection::
onParserResponseStart(const std::string & httpVersion, int code)
{
    // cerr << "onParserResponseStart: " << this << endl;
    request_.callbacks().onResponseStart(request_, httpVersion, code);
}

void
HttpConnection::
onParserHeader(const char * data, size_t size)
{
    // cerr << "onParserHeader: " << this << endl;
    request_.callbacks().onHeader(request_, data, size);
}

void
HttpConnection::
onParserData(const char * data, size_t size)
{
    // cerr << "onParserData: " << this << endl;
    request_.callbacks().onData(request_, data, size);
}

void
HttpConnection::
onParserDone()
{
    handleEndOfRq(0);
}

void
HttpConnection::
handleEndOfRq(int code)
{
    // cerr << "handleEndOfRq: " << this << endl;
    if (code != 0) {
        requestClose();
    }
    request_.callbacks().onDone(request_, code);
    clear();
    onDone(code);
}


/* HTTPCLIENT */

HttpClient::
HttpClient(const string & baseUrl, int numParallel, size_t queueSize)
    : MessageLoop(1, 0, -1),
      noSSLChecks(false),
      baseUrl_(baseUrl),
      debug_(false),
      connectionStash_(numParallel),
      avlConnections_(numParallel),
      nextAvail_(0),
      queue_(queueSize)
{
    queue_.onEvent = [&] (HttpRequest && rq) {
        // cerr << "onEvent\n";
        this->handleQueueEvent(move(rq));
    };
    addSource("queue", queue_);

    /* available connections */
    for (size_t i = 0; i < connectionStash_.size(); i++) {
        connectionStash_[i].init(baseUrl);
        connectionStash_[i].onDone = [&,i] (int result) {
            // cerr << "connection " + to_string(i) + " onDone: "
                 // << &connectionStash_[i] << endl;
            handleHttpConnectionDone(connectionStash_[i], result);
        };
        addSource("socket" + to_string(i), connectionStash_[i]);
        avlConnections_[i] = &connectionStash_[i];
    }
}

HttpClient::
~HttpClient()
{
    // cerr << "~HttpClient: " << this << "\n";
    shutdown();
}

void
HttpClient::
shutdown()
{
    if (connectionState_ == AsyncEventSource::CONNECTED) {
        removeSource(&queue_);
        for (size_t i = 0; i < connectionStash_.size(); i++) {
            removeSource(&connectionStash_[i]);
        }

        queue_.waitConnectionState(AsyncEventSource::DISCONNECTED);
        for (size_t i = 0; i < connectionStash_.size(); i++) {
            connectionStash_[i].waitConnectionState(AsyncEventSource::DISCONNECTED);
        }
    }

    MessageLoop::shutdown();
}

void
HttpClient::
enablePipelining()
{
    throw ML::Exception("unimplemented");
    // ::curl_multi_setopt(handle_, CURLMOPT_PIPELINING, 1);
}

void
HttpClient::
debug(bool debugOn)
{
    debug_ = debugOn;
    MessageLoop::debug(debugOn);
}

bool
HttpClient::
enqueueRequest(const string & verb, const string & resource,
               const shared_ptr<HttpClientCallbacks> & callbacks,
               const MimeContent & content,
               const RestParams & queryParams, const RestParams & headers,
               int timeout)
{
    string url = baseUrl_ + resource + queryParams.uriEscaped();
    bool res = queue_.tryPush(HttpRequest(verb, url, callbacks,
                                          content, headers, timeout));

    return res;
}

void
HttpClient::
handleQueueEvent(HttpRequest && request)
{
    if (nextAvail_ < avlConnections_.size()) {
        if (inThreadQueue_.size() > 0) {
            cerr << "BAD: connections available while there are elements in the queue?\n";
        }
        HttpConnection * conn = getConnection();
        conn->perform(move(request));
    }
    else {
        // cerr << "enqueuing request in thread\n";
        inThreadQueue_.emplace_back(move(request));
    }
}

void
HttpClient::
handleHttpConnectionDone(HttpConnection & connection, int result)
{
    if (inThreadQueue_.size() > 0) {
        // cerr << "emptying queue...\n";
        connection.perform(move(inThreadQueue_.front()));
        inThreadQueue_.pop_front();
    }
    else {
        releaseConnection(&connection);
    }
}

HttpConnection *
HttpClient::
getConnection()
{
    HttpConnection * conn;

    if (nextAvail_ < avlConnections_.size()) {
        conn = avlConnections_[nextAvail_];
        nextAvail_++;
    }
    else {
        conn = nullptr;
    }

    // cerr << " returning conn: " << conn << "\n";

    return conn;
}

void
HttpClient::
releaseConnection(HttpConnection * oldConnection)
{
    if (nextAvail_ > 0) {
        nextAvail_--;
        avlConnections_[nextAvail_] = oldConnection;
    }
}


/* HTTPCLIENTSIMPLECALLBACKS */

HttpClientSimpleCallbacks::
HttpClientSimpleCallbacks(const OnResponse & onResponse)
    : onResponse_(onResponse), statusCode_(0)
{
}

void
HttpClientSimpleCallbacks::
onResponseStart(const HttpRequest & rq,
                const string & httpVersion, int code)
{
    statusCode_ = code;
}

void
HttpClientSimpleCallbacks::
onHeader(const HttpRequest & rq, const char * data, size_t size)
{
    headers_.append(data, size);
}

void
HttpClientSimpleCallbacks::
onData(const HttpRequest & rq, const char * data, size_t size)
{
    body_.append(data, size);
}

void
HttpClientSimpleCallbacks::
onDone(const HttpRequest & rq, int error)
{
    onResponse(rq, error, statusCode_, move(headers_), move(body_));
}

void
HttpClientSimpleCallbacks::
onResponse(const HttpRequest & rq,
           int error, int status,
           string && headers, string && body)
{
    if (onResponse_) {
        onResponse_(rq, error, status, move(headers), move(body));
    }
    statusCode_ = 0;
    headers_.clear();
    body_.clear();
}
