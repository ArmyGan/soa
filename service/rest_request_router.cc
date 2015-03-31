/* rest_request_router.cc
   Jeremy Barnes, 15 November 2012
   Copyright (c) 2012 Datacratic Inc.  All rights reserved.

*/

#include "rest_request_router.h"
#include "fs_utils.h"
#include "jml/utils/vector_utils.h"
#include "jml/arch/exception_handler.h"
#include "jml/utils/set_utils.h"
#include "jml/utils/environment.h"
#include "jml/utils/file_functions.h"
#include "jml/utils/string_functions.h"


using namespace std;


namespace Datacratic {


/*****************************************************************************/
/* PATH SPEC                                                                 */
/*****************************************************************************/

PathSpec::
PathSpec()
    : type(NONE)
{
}
        
PathSpec::
PathSpec(const std::string & fullPath)
    : type(STRING), path(fullPath)
{
}

PathSpec::
PathSpec(const char * fullPath)
    : type(STRING), path(fullPath)
{
}

PathSpec::
PathSpec(const std::string & str, const boost::regex & rex)
    : type(REGEX),
      path(str),
      rex(rex)
{
}

void
PathSpec::
getHelp(Json::Value & result) const
{
    switch (type) {
    case STRING:
        result["path"] = path;
        break;
    case REGEX: {
        Json::Value & v = result["path"];
        v["regex"] = path;
        v["desc"] = desc;
        break;
    }
    default:
        throw ML::Exception("unknown path parameter");
    }
}
    
std::string
PathSpec::
getPathDesc() const
{
    if (!desc.empty())
        return desc;
    return path;
}

int
PathSpec::
numCapturedElements() const
{
    switch (type) {
    case NONE: return 0;
    case STRING: return 1;
    case REGEX: return rex.mark_count() + 1;
    default:
        throw ML::Exception("unknown mark count");
    }
}

bool
PathSpec::
operator == (const PathSpec & other) const
{
    return path == other.path;
}

bool
PathSpec::
operator != (const PathSpec & other) const
{
    return ! operator == (other);
}

bool
PathSpec::
operator < (const PathSpec & other) const
{
    return path < other.path;
}

PathSpec
Rx(const std::string & regexString, const std::string & desc)
{
    PathSpec result(regexString, boost::regex(regexString));
    result.desc = desc;
    return result;
}

std::ostream & operator << (std::ostream & stream, const PathSpec & path)
{
    return stream << path.path;
}
               

/*****************************************************************************/
/* REQUEST FILTER                                                            */
/*****************************************************************************/

/** Filter for a REST request by method, etc. */

RequestFilter::
RequestFilter()
{
}

RequestFilter::
RequestFilter(const std::string & verb)
{
    verbs.insert(verb);
    parseVerbs();
}

RequestFilter::
RequestFilter(const char * verb)
{
    verbs.insert(verb);
    parseVerbs();
}

RequestFilter::
RequestFilter(std::set<std::string> verbs)
    : verbs(std::move(verbs))
{
    parseVerbs();
}

RequestFilter::
RequestFilter(const std::initializer_list<std::string> & verbs)
    : verbs(verbs)
{
    parseVerbs();
}

void
RequestFilter::
parseVerbs()
{
    std::set<std::string> newVerbs;
        
    for (auto & v: verbs) {
        auto i = v.find('=');

        if (i == std::string::npos) {
            newVerbs.insert(v);
            continue;
        }
            
        std::string key(v, 0, i);
        std::string value(v, i + 1);

        RequestParamFilter::Location location
            = RequestParamFilter::QUERY;

        if (key.find("header:") == 0) {
            location = RequestParamFilter::HEADER;
            key = string(key, 7);  // strip off header:
        }
        
        filters.emplace_back(location, key, value);
    }

    newVerbs = verbs;
}

void
RequestFilter::
getHelp(Json::Value & result) const
{
    if (!verbs.empty()) {
        int i = 0;
        for (auto it = verbs.begin(), end = verbs.end(); it != end;  ++it, ++i) {
            result["verbs"][i] = *it;
        }
    }
    if (!filters.empty()) {
        for (auto & f: filters) {
            string loc = (f.location == RequestParamFilter::HEADER ? "header:" : "");
            result["filters"].append(loc + f.param + "=" + f.value);
        }
    }
}

std::ostream & operator << (std::ostream & stream, const RequestFilter & filter)
{
    return stream;
}


/*****************************************************************************/
/* REST REQUEST PARSING CONTEXT                                              */
/*****************************************************************************/

std::ostream & operator << (std::ostream & stream,
                            const RestRequestParsingContext & context)
{
    return stream << context.resources << " " << context.remaining;
}


/*****************************************************************************/
/* REST REQUEST ROUTER                                                       */
/*****************************************************************************/

RestRequestRouter::
RestRequestRouter()
    : terminal(false)
{
}

RestRequestRouter::
RestRequestRouter(const OnProcessRequest & processRequest,
                  const std::string & description,
                  bool terminal,
                  const Json::Value & argHelp)
    : rootHandler(processRequest),
      description(description),
      terminal(terminal),
      argHelp(argHelp)
{
}

RestRequestRouter::
~RestRequestRouter()
{
}
    
RestRequestRouter::OnHandleRequest
RestRequestRouter::
requestHandler() const
{
    return std::bind(&RestRequestRouter::handleRequest,
                     this,
                     std::placeholders::_1,
                     std::placeholders::_2);
}

void
RestRequestRouter::
handleRequest(RestConnection & connection,
              const RestRequest & request) const
{
    //JML_TRACE_EXCEPTIONS(false);

    RestRequestParsingContext context(request);
    MatchResult res = processRequest(connection, request, context);
    if (res == MR_NO) {
        connection.sendErrorResponse(404, "unknown resource " + request.verb + " " + request.resource);
    }
}

static std::string getVerbsStr(const std::set<std::string> & verbs)
{
    string verbsStr;
    for (auto v: verbs) {
        if (!verbsStr.empty())
            verbsStr += ",";
        verbsStr += v;
    }
            
    return verbsStr;
}

namespace {

ML::Env_Option<bool, true> TRACE_REST_REQUESTS("TRACE_REST_REQUESTS", false);

} // file scope

RestRequestRouter::
MatchResult
RestRequestRouter::
processRequest(RestConnection & connection,
               const RestRequest & request,
               RestRequestParsingContext & context) const
{
    bool debug = TRACE_REST_REQUESTS;

    if (debug) {
        cerr << "processing request " << request
             << " with context " << context
             << " against route " << description 
             << " with " << subRoutes.size() << " subroutes" << endl;
    }

    if (request.verb == "OPTIONS") {
        Json::Value help;
        std::set<std::string> verbs;

        this->options(verbs, help, request, context);

        RestParams headers = { { "Allow", getVerbsStr(verbs) } };
        
        if (verbs.empty())
            connection.sendHttpResponse(400, "", "", headers);
        else
            connection.sendHttpResponse(200, help.toStyledString(),
                                        "application/json",
                                        headers);
        return MR_YES;
    }

    if (rootHandler && (!terminal || context.remaining.empty()))
        return rootHandler(connection, request, context);

    for (auto & sr: subRoutes) {
        if (debug)
            cerr << "  trying subroute " << sr.router->description << endl;
        try {
            MatchResult mr = sr.process(request, context, connection);
            //cerr << "returned " << mr << endl;
            if (mr == MR_YES || mr == MR_ASYNC || mr == MR_ERROR)
                return mr;
        } catch (const std::exception & exc) {
            connection.sendErrorResponse(500, ML::format("threw exception: %s",
                                                         exc.what()));
            return MR_YES;
        } catch (...) {
            connection.sendErrorResponse(500, "unknown exception");
            return MR_YES;
        }
    }

    return MR_NO;
    //connection.sendErrorResponse(404, "invalid route for "
    //                             + request.resource);
}

void
RestRequestRouter::
options(std::set<std::string> & verbsAccepted,
        Json::Value & help,
        const RestRequest & request,
        RestRequestParsingContext & context) const
{
    for (auto & sr: subRoutes) {
        sr.options(verbsAccepted, help, request, context);
    }
}

bool
RestRequestRouter::Route::
matchPath(const RestRequest & request,
          RestRequestParsingContext & context) const
{
    switch (path.type) {
    case PathSpec::STRING: {
        std::string::size_type pos = context.remaining.find(path.path);
        if (pos == 0) {
            using namespace std;
            //cerr << "context string " << pos << endl;
            context.resources.push_back(path.path);
            context.remaining = string(context.remaining, path.path.size());
            break;
        }
        else return false;
    }
    case PathSpec::REGEX: {
        boost::smatch results;
        bool found
            = boost::regex_search(context.remaining,
                                  results,
                                  path.rex)
            && !results.prefix().matched;  // matches from the start
        
        //cerr << "matching regex " << path.path << " against "
        //     << context.remaining << " with found " << found << endl;
        if (!found)
            return false;
        for (unsigned i = 0;  i < results.size();  ++i)
            context.resources.push_back(results[i]);
        context.remaining = std::string(context.remaining,
                                        results[0].length());
        break;
    }
    case PathSpec::NONE:
    default:
        throw ML::Exception("unknown rest request type");
    }

    return true;
}

RestRequestRouter::MatchResult
RestRequestRouter::Route::
process(const RestRequest & request,
        RestRequestParsingContext & context,
        RestConnection & connection) const
{
    using namespace std;

    bool debug = TRACE_REST_REQUESTS;

    if (debug) {
        cerr << "verb = " << request.verb << " filter.verbs = " << filter.verbs
             << endl;
    }
    if (!filter.verbs.empty()
        && !filter.verbs.count(request.verb))
        return MR_NO;

    // Check that the parameter filters match
    for (auto & f: filter.filters) {
        bool matched = false;

        if (f.location == RequestParamFilter::QUERY) {
            for (auto & p: request.params) {
                if (p.first == f.param && p.second == f.value) {
                    matched = true;
                    break;
                }
            }
        }
        else if (f.location == RequestParamFilter::HEADER) {
            //cerr << "matching header " << f.param << " with value "
            //     << request.header.tryGetHeader(f.param)
            //     << " against " << f.value << endl;
            if (request.header.tryGetHeader(f.param) == f.value) {
                matched = true;
            }
        }

        if (!matched)
            return MR_NO;
    }

    // At the end, make sure we put the context back to how it was
    RestRequestParsingContext::StateGuard guard(&context);

    if (!matchPath(request, context))
        return MR_NO;

    if (extractObject)
        extractObject(connection, request, context);

    if (connection.responseSent())
        return MR_YES;

    return router->processRequest(connection, request, context);
}

void
RestRequestRouter::Route::
options(std::set<std::string> & verbsAccepted,
        Json::Value & help,
        const RestRequest & request,
        RestRequestParsingContext & context) const
{
    RestRequestParsingContext::StateGuard guard(&context);

    if (!matchPath(request, context))
        return;

    if (context.remaining.empty()) {
        verbsAccepted.insert(filter.verbs.begin(), filter.verbs.end());

        string path = "";//this->path.getPathDesc();
        Json::Value & sri = help[path + getVerbsStr(filter.verbs)];
        this->path.getHelp(sri);
        filter.getHelp(sri);
        router->getHelp(help, path, filter.verbs);
    }
    router->options(verbsAccepted, help, request, context);
}

void
RestRequestRouter::
addRoute(PathSpec path, RequestFilter filter,
         const std::shared_ptr<RestRequestRouter> & handler,
         ExtractObject extractObject)
{
    if (rootHandler)
        throw ML::Exception("can't add a sub-route to a terminal route");

    Route route;
    route.path = path;
    route.filter = filter;
    route.router = handler;
    route.extractObject = extractObject;

    subRoutes.emplace_back(std::move(route));
}

void
RestRequestRouter::
addRoute(PathSpec path, RequestFilter filter,
         const std::string & description,
         const OnProcessRequest & cb,
         const Json::Value & argHelp,
         ExtractObject extractObject)
{
    addRoute(path, filter,
             std::make_shared<RestRequestRouter>(cb, description, true, argHelp),
             extractObject);
}

void
RestRequestRouter::
addHelpRoute(PathSpec path, RequestFilter filter)
{
    OnProcessRequest helpRoute
        = [=] (RestConnection & connection,
               const RestRequest & request,
               const RestRequestParsingContext & context)
        {
            Json::Value help;
            if (request.params.hasValue("autodoc")) {
                getAutodocHelp(help, "", set<string>());
            } else {
                getHelp(help, "", set<string>());
            }
            connection.sendResponse(200, help);
            return MR_YES;
        };

    addRoute(path, filter, "Get help on the available API commands",
             helpRoute, Json::Value());
}


void
RestRequestRouter::
addAutodocRoute(PathSpec autodocPath, PathSpec helpPath,
                const string & autodocFilesPath)
{
    string autodocPathStr = autodocPath.getPathDesc();
    OnProcessRequest rootRoute
        = [=] (RestConnection & connection,
               const RestRequest & request,
               const RestRequestParsingContext & context) {
        connection.sendRedirect(302, autodocPathStr + "/index.html");
        return RestRequestRouter::MR_YES;
    };

    addRoute(autodocPathStr, "GET", "Main autodoc page",
             rootRoute, Json::Value());
    addRoute(autodocPathStr + "/", "GET", "Main autodoc page",
             rootRoute, Json::Value());

    OnProcessRequest autodocRoute
        = [=] (RestConnection & connection,
               const RestRequest & request,
               const RestRequestParsingContext & context) {

        string path = context.resources.back();

        if (path.find("..") != string::npos) {
            throw ML::Exception("not dealing with path with .. in it");
        }

        if (path.find(autodocPathStr) != 0) {
            throw ML::Exception("not serving file not under %",
                                autodocPathStr.c_str());
        }

        string filename = path.substr(autodocPathStr.size());
        if (filename[0] == '/') {
            filename = filename.substr(1);
        }
        if (filename == "autodoc") {
            connection.sendRedirect(302, helpPath.getPathDesc() + "?autodoc");
            return RestRequestRouter::MR_YES;
        }

        ML::File_Read_Buffer buf(autodocFilesPath + "/" + filename);

        string mimeType = "text/plain";
        if (filename.find(".html") != string::npos) {
            mimeType = "text/html";
        }
        else if (filename.find(".js") != string::npos) {
            mimeType = "application/javascript";
        }
        else if (filename.find(".css") != string::npos) {
            mimeType = "text/css";
        }

        string result(buf.start(), buf.end());
        connection.sendResponse(200, result,  mimeType);
        return RestRequestRouter::MR_YES;
    };

    addRoute(Rx(autodocPathStr + "/.*", "<resource>"), "GET",
            "Static content", autodocRoute, Json::Value());
}

void
RestRequestRouter::
getHelp(Json::Value & result, const std::string & currentPath,
        const std::set<std::string> & verbs) const
{
    Json::Value & v = result[(currentPath.empty() ? "" : currentPath + " ")
                             + getVerbsStr(verbs)];

    v["description"] = description;
    if (!argHelp.isNull())
        v["arguments"] = argHelp;
    
    for (unsigned i = 0;  i < subRoutes.size();  ++i) {
        string path = currentPath + subRoutes[i].path.getPathDesc();
        Json::Value & sri = result[(path.empty() ? "" : path + " ")
                                   + getVerbsStr(subRoutes[i].filter.verbs)];
        subRoutes[i].path.getHelp(sri);
        subRoutes[i].filter.getHelp(sri);
        subRoutes[i].router->getHelp(result, path, subRoutes[i].filter.verbs);
    }
}

void
RestRequestRouter::
updateFromValueDescription(Json::Value & v, const ValueDescription * vd) const {
    const ValueKind kind = vd->kind;
    if (kind == ValueKind::INTEGER) {
        v["type"] = "integer";
    }
    else if (kind == ValueKind::BOOLEAN) {
        v["type"] = "boolean";
    }
    else if (kind == ValueKind::STRING) {
        v["type"] = "string";
    }
    else if (kind == ValueKind::ENUM) {
        v["description"].asString() + " (cppType: " + vd->typeName + ")";
        v["type"] = "string";
        vector<string> keys = vd->getEnumKeys();
        stringstream pattern;
        bool first = true;
        for (const string & k: keys) {
            if (!first) {
                pattern << "|";
            }
            pattern << k;
        };
        v["pattern"] = pattern.str();
    }
    else if (kind == ValueKind::LINK) {
        cerr << "Got link field as final value: " << vd->typeName << endl;
        v["description"].asString() + " (cppType: " + vd->typeName + ")";
        const ValueDescription * subVdPtr = &(vd->contained());
        cerr << subVdPtr->typeName << endl;
        v["type"] = "string";
    }
    else if (kind == ValueKind::FLOAT) {
        v["type"] = "float";
    }
    else if (kind == ValueKind::ARRAY) {
        v["type"] = "array";
        const ValueDescription * subVdPtr = &(vd->contained());
        updateFromValueDescription(v["items"], subVdPtr);
    }
    else if (kind == ValueKind::STRUCTURE) {
        v["description"].asString() + " (cppType: " + vd->typeName + ")";
        v["type"] = "object";
    }
    else if (kind == ValueKind::ATOM) {
        v["description"] =
            v["description"].asString() + " (cppType: " + vd->typeName + ")";
        if (vd->typeName == "Datacratic::TimePeriod") {
            v["type"] = "string";
            v["pattern"] = "^[\\d]+(s|m|h|d)$";
        }
        else if (vd->typeName == "Datacratic::Any") {
            v["type"] = "object";
        }
        else {
            v["type"] = "string";
        }
    }
    else if (kind == ValueKind::ANY) {
        //cppType == Json::Value
        v["type"] = "object";
    }
    else {
        cerr << "uncovered conversion case for kind: " << kind
             << " typeName: " << vd->typeName << endl;
        v["type"] = "object (cppType: " + vd->typeName + ")";
    }
}


void
RestRequestRouter::
addValueDescriptionToProperties(const ValueDescription * vd,
                                Json::Value & properties, int recur) const
{
    using namespace Json;
    if (recur > 2) {
        //Too many recursions
        return;
    }

    auto onField = [&] (const ValueDescription::FieldDescription & fd) {
        Value tmpObj;
        tmpObj["description"] = fd.comment;
        const ValueDescription * curr = fd.description.get();
        if (curr->kind == ValueKind::LINK) {
            curr = &(curr->contained());
            if (curr->kind == ValueKind::LINK) {
                cerr << "link of link not supported" << endl;
            }
        }
        updateFromValueDescription(tmpObj, curr);
        if (curr->kind == ValueKind::ARRAY) {
            const ValueDescription * subVdPtr = &(curr->contained());
            if (subVdPtr->kind == ValueKind::STRUCTURE) {
                if (vd == subVdPtr) {
                    tmpObj["items"]["type"] =
                        "object (recursive, cppType: " + curr->typeName + ")";
                    tmpObj["items"]["properties"] = objectValue;
                }
                else {
                    Value itemProperties;
                    addValueDescriptionToProperties(subVdPtr, itemProperties,
                                                    recur + 1);
                    tmpObj["items"]["items"]["properties"] = itemProperties;
                }
            }
            else {
                if (subVdPtr->kind == ValueKind::ARRAY) {
                    // unsupported "pair" type
                    tmpObj["items"]["type"] =
                        "object (cppType: " + curr->typeName + ")";
                }
                else {
                    updateFromValueDescription(tmpObj["items"], subVdPtr);
                }
            }
        }
        else if (curr->kind == ValueKind::STRUCTURE) {
            Value itemProperties;
            addValueDescriptionToProperties(curr,
                                            itemProperties, recur + 1);
            tmpObj["items"]["properties"] = itemProperties;
        }
        properties[fd.fieldName] = tmpObj;
    };
    vd->forEachField(nullptr, onField);
}


void
RestRequestRouter::
addJsonParamsToProperties(const Json::Value & params,
                          Json::Value & properties) const
{
    using namespace Json;
    for (Value param: params) {
        string cppType = param["cppType"].asString();
        const ValueDescription * vd = ValueDescription::get(cppType).get();
        if (vd->kind == ValueKind::STRUCTURE) {
            addValueDescriptionToProperties(vd, properties);
        }
        else {
            Value tmpObj;
            updateFromValueDescription(tmpObj, vd);
            tmpObj["description"] = param["description"].asString();
            properties[param["name"].asString()] = tmpObj;
        }
    }
}

void
RestRequestRouter::
getAutodocHelp(Json::Value & result, const std::string & currentPath,
               const std::set<std::string> & verbs) const
{
    using namespace Json;
    Value tmpResult;
    getHelp(tmpResult, "", set<string>());
    result["routes"]   = arrayValue;
    result["literate"] = arrayValue;
    result["config"]   = objectValue;
    for (ValueIterator it = tmpResult.begin() ; it != tmpResult.end() ; it++) {
        string key = it.key().asString();
        vector<string> parts = ML::split(it.key().asString());
        int size = parts.size();
        if (size == 0) {
            // the empty key contains the description
            continue;
        }
        if (size == 1) {
            // useless route
            continue;
        }
        ExcAssert(size == 2);
        if (parts[1] != "GET" && parts[1] != "POST" && parts[1] != "PUT"
                && parts[1] != "DELETE") {
            //unsupported verb + param
            continue;
        }

        Value curr = arrayValue;
        curr.append(parts[1] + " " + parts[0]);
        Value subObj;
        subObj["out"] = objectValue;
        subObj["out"]["required"] = arrayValue;
        subObj["out"]["type"] = "object";
        subObj["out"]["properties"] = objectValue;
        subObj["required_role"] = nullValue;
        subObj["docstring"] = (*it)["description"].asString();
        subObj["in"] = nullValue;
        subObj["in"]["required"] = arrayValue;
        subObj["in"]["type"] = "object";
        subObj["in"]["properties"] = objectValue;
        if ((*it).isMember("arguments") && (*it)["arguments"].isMember("jsonParams")) {
            addJsonParamsToProperties((*it)["arguments"]["jsonParams"],
                                      subObj["in"]["properties"]);
        }
        curr.append(subObj);
        result["routes"].append(curr);
    }
}

RestRequestRouter &
RestRequestRouter::
addSubRouter(PathSpec path, const std::string & description, ExtractObject extractObject,
             std::shared_ptr<RestRequestRouter> subRouter)
{
    // TODO: check it doesn't exist
    Route route;
    route.path = path;
    if (subRouter)
        route.router = subRouter;
    else route.router.reset(new RestRequestRouter());

    route.router->description = description;
    route.extractObject = extractObject;

    subRoutes.push_back(route);
    return *route.router;
}

RestRequestRouter::OnProcessRequest
RestRequestRouter::
getStaticRouteHandler(const string & dir) const {
    RestRequestRouter::OnProcessRequest staticRoute
        = [dir] (RestConnection & connection,
                 const RestRequest & request,
                 const RestRequestParsingContext & context) {

        string path = context.resources.back();

        //cerr << "static content for " << path << endl;

        if (path.find("..") != string::npos) {
            throw ML::Exception("not dealing with path with .. in it");
        }

        string filename = dir + "/" + path;

        if (!tryGetUriObjectInfo(filename)) {
            connection.sendErrorResponse
                (404,
                 "File '" + filename + "' doesn't exist", "text/plain");
            return MR_YES;
        }

        ML::filter_istream stream(filename);

        ML::File_Read_Buffer buf(filename);

        string mimeType = "text/plain";
        if (filename.find(".html") != string::npos) {
            mimeType = "text/html";
        }
        else if (filename.find(".js") != string::npos) {
            mimeType = "application/javascript";
        }
        else if (filename.find(".css") != string::npos) {
            mimeType = "text/css";
        }

        string result(buf.start(), buf.end());
        connection.sendResponse(200, result, mimeType);
        return RestRequestRouter::MR_YES;
    };
    return staticRoute;
}

void RestRequestRouter::
serveStaticDirectory(const std::string & route, const std::string & dir) {
    addRoute(Rx(route + "/(.*)", "<resource>"),
             "GET", "Static content",
             getStaticRouteHandler(dir),
             Json::Value());
}


} // namespace Datacratic
