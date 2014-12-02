/* http_named_endpoint.cc
   Jeremy Barnes, 11 November 2012
   Copyright (c) 2012 Datacratic Inc.  All rights reserved.

   Named endpoint for http connections.
*/

#include "http_named_endpoint.h"

using namespace std;

namespace Datacratic {


/*****************************************************************************/
/* HTTP NAMED ENDPOINT                                                       */
/*****************************************************************************/

HttpNamedEndpoint::
HttpNamedEndpoint()
{
}

void
HttpNamedEndpoint::
init(std::shared_ptr<ConfigurationService> config,
          const std::string & endpointName)
{
    NamedEndpoint::init(config, endpointName);
}

std::string
HttpNamedEndpoint::
bindTcp(PortRange const & portRange, std::string host)
{
    using namespace std;

    // TODO: generalize this...
    if (host == "" || host == "*")
        host = "0.0.0.0";

    // TODO: really scan ports
    int port = HttpEndpoint::listen(portRange, host, false /* name lookup */);

    cerr << "bound tcp for http port " << port << endl;

    auto getUri = [&] (const std::string & host)
        {
            return "http://" + host + ":" + to_string(port);
        };

    Json::Value config;

    auto addEntry = [&] (const std::string & addr,
                         const std::string & hostScope,
                         const std::string & uri)
        {
            Json::Value & entry = config[config.size()];
            entry["httpUri"] = uri;

            Json::Value & transports = entry["transports"];
            transports[0]["name"] = "tcp";
            transports[0]["addr"] = addr;
            transports[0]["hostScope"] = hostScope;
            transports[0]["port"] = port;
            transports[1]["name"] = "http";
            transports[1]["uri"] = uri;
        };

    if (host == "0.0.0.0") {
        auto interfaces = getInterfaces({AF_INET});
        for (unsigned i = 0;  i < interfaces.size();  ++i) {
            addEntry(interfaces[i].addr,
                     interfaces[i].hostScope,
                     getUri(interfaces[i].addr));
        }
        publishAddress("tcp", config);
        return getUri(host);
    }
    else {
        string host2 = addrToIp(host);
        string uri = getUri(host2);
        // TODO: compute host scope
        addEntry(host2, "*", uri);
        publishAddress("tcp", config);
        return uri;
    }
}


/*****************************************************************************/
/* HTTP NAMED REST PROXY                                                     */
/*****************************************************************************/

void
HttpNamedRestProxy::
init(std::shared_ptr<ConfigurationService> config)
{
    this->config = config;
}

bool
HttpNamedRestProxy::
connectToServiceClass(const std::string & serviceClass,
                      const std::string & endpointName,
                      bool local)
{
    this->serviceClass = serviceClass;
    this->endpointName = endpointName;

    std::vector<std::string> children
        = config->getChildren("serviceClass/" + serviceClass);

    for (auto c : children) {
        std::string key = "serviceClass/" + serviceClass + "/" + c;

        Json::Value value = config->getJson(key);
        std::string name = value["serviceName"].asString();
        std::string path = value["servicePath"].asString();

        std::string location = value["serviceLocation"].asString();
        if (local && location != config->currentLocation)
            continue;

        //cerr << "name = " << name << " path = " << path << endl;
        if (connect(path + "/" + endpointName))
            break;
    }

    return connected;
}

bool
HttpNamedRestProxy::
connect(const std::string & endpointName)
{
    using namespace std;

    // auto onChange = std::bind(&HttpNamedRestProxy::onConfigChange, this,
    //                           std::placeholders::_1,
    //                           std::placeholders::_2,
    //                           std::placeholders::_3);

    connected = false;

    // 2.  Iterate over all of the connection possibilities until we
    //     find one that works.
    auto onConnection = [&] (const std::string & key,
                             const Json::Value & epConfig) -> bool
        {
            if (connected)
                return false;
            //cerr << "epConfig for " << key << " is " << epConfig
            //<< endl;
                
            for (auto & entry: epConfig) {

                //cerr << "entry is " << entry << endl;

                if (!entry.isMember("httpUri"))
                    return true;

                string uri = entry["httpUri"].asString();

                cerr << "uri = " << uri << endl;

                auto hs = entry["transports"][0]["hostScope"];
                if (!hs)
                    continue;

                // TODO: allow localhost connections on localhost
                string hostScope = hs.asString();
                if (hs != "*") {
                    utsname name;
                    if (uname(&name))
                        throw ML::Exception(errno, "uname");
                    if (hostScope != name.nodename)
                        continue;  // wrong host scope
                }

                serviceUri = uri;

                cerr << "connected to " << uri << endl;
                connected = true;

                // Continue the connection in the onConfigChange function
                onConfigChange(ConfigurationService::VALUE_CHANGED,
                               key,
                               epConfig);
                return false;
            }

            return false;
        };

    config->forEachEntry(onConnection, endpointName);
    return connected;
}

/** Called back when one of our endpoints either changes or disappears. */
bool
HttpNamedRestProxy::
onConfigChange(ConfigurationService::ChangeType change,
               const std::string & key,
               const Json::Value & newValue)
{
    using namespace std;

    cerr << "config for " << key << " has changed" << endl;

#if 0
    // 3.  Find an appropriate entry to connect to
    for (auto & entries: newValue) {
        // TODO: connect
        cerr << "got entries " << entries << endl;
    }
#endif

    return true;
}


} // namespace Datacratic
