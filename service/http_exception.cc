/** http_exception.cc
    Jeremy Barnes, 2 April 2015

*/

#include "http_exception.h"
#include "soa/types/basic_value_descriptions.h"


using namespace std;


namespace Datacratic {

void rethrowHttpException(int httpCode, const Utf8String & message, Any details)
{
    try {
        JML_TRACE_EXCEPTIONS(false);
        std::rethrow_exception(std::current_exception());
    } catch (const HttpReturnException & http) {
        Json::Value details2 = jsonEncode(details);
        details2["context"]["details"] = jsonEncode(http.details);
        details2["context"]["error"] = http.message;
        throw HttpReturnException(httpCode == -1 ? http.httpCode : httpCode, message, details2);
    } catch (const std::exception & exc) {
        Json::Value details2 = jsonEncode(details);
        details2["context"]["error"] = exc.what();
        throw HttpReturnException(httpCode == -1 ? 400 : httpCode, message, details2);
    }

    throw HttpReturnException(httpCode == -1 ? 400 : httpCode, message, details);
}

void rethrowHttpException(int httpCode, const std::string & message, Any details)
{
    rethrowHttpException(httpCode, Utf8String(message), details);
}

} // namespace Datacratic
