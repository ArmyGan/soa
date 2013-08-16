/* sqs.cc
   Jeremy Barnes, 12 May 2013
   Copyright (c) 2013 Datacratic Inc.  All rights reserved.

   Basic interface to Amazon's SQS service.
*/

#include "sqs.h"
#include "xml_helpers.h"
#include <boost/algorithm/string.hpp>
#include "jml/utils/exc_assert.h"


using namespace std;
using namespace ML;


namespace {

using namespace Datacratic;


SqsApi::Message
extractMessage(const tinyxml2::XMLNode * messageNode)
{
    SqsApi::Message message;

    message.body = extract<string>(messageNode, "Body");
    message.bodyMd5 = extract<string>(messageNode,
                                      "MD5OfBody");
    message.messageId = extract<string>(messageNode,
                                        "MessageId");
    message.receiptHandle = extract<string>(messageNode,
                                            "ReceiptHandle");

    // xml->Print();

    const tinyxml2::XMLElement * p = extractNode(messageNode, "Attribute")->ToElement();
    while (p && strcmp(p->Name(), "Attribute") == 0) {
        const tinyxml2::XMLNode * name = extractNode(p, "Name");
        const tinyxml2::XMLNode * value = extractNode(p, "Value");
        if (name && value) {
            string attrName(name->FirstChild()->ToText()->Value());
            string attrValue(value->FirstChild()->ToText()->Value());
            if (value) {
                if (attrName == "SenderId") {
                    message.senderId = attrValue;
                }
                else if (attrName == "ApproximateFirstReceiveTimestamp") {
                    long long ms = stoll(attrValue);
                    double seconds = (double)ms / 1000;
                    message.approximateFirstReceiveTimestamp
                        = Date::fromSecondsSinceEpoch(seconds);
                }
                else if (attrName == "SentTimestamp") {
                    long long ms = stoll(attrValue);
                    double seconds = (double)ms / 1000;
                    message.sentTimestamp
                        = Date::fromSecondsSinceEpoch(seconds);
                }
                else if (attrName == "ApproximateReceiveCount") {
                    message.approximateReceiveCount = stoi(attrValue);
                }
                else {
                    throw ML::Exception("unexpected attribute name: "
                                        + attrName);
                }
            }
        }
        p = p->NextSiblingElement();
    }

    return message;
}

}


namespace Datacratic {


/*****************************************************************************/
/* SQS API                                                                   */
/*****************************************************************************/

SqsApi::
SqsApi(const std::string & protocol, const std::string & region)
{
    setService("sqs", protocol, region);
}

std::string
SqsApi::
createQueue(const std::string & queueName,
            const QueueParams & params)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "CreateQueue"});
    queryParams.push_back({"QueueName", queueName});
    queryParams.push_back({"Version", "2012-11-05"});

    int counter(1);
    auto addAttribute = [&] (const string & name, const string & value) {
        string prefix = "Attribute." + to_string(counter);
        queryParams.push_back({prefix + ".Name", name});
        queryParams.push_back({prefix + ".Value", value});
        counter++;
    };

    if (params.delaySeconds > 0) {
        addAttribute("DelaySeconds", to_string(params.delaySeconds));
    }
    if (params.maximumMessageSize > -1) {
        addAttribute("MaximumMessageSize",
                     to_string(params.maximumMessageSize));
    }
    if (params.messageRetentionPeriod > -1) {
        addAttribute("MessageRetentionPeriod",
                     to_string(params.messageRetentionPeriod));
    }
    if (params.policy.size() > 0) {
        throw ML::Exception("'policy' not supported yet");
    }
    if (params.receiveMessageWaitTimeSeconds > -1) {
        addAttribute("ReceiveMessageWaitTimeSeconds",
                     to_string(params.receiveMessageWaitTimeSeconds));
    }
    if (params.visibilityTimeout > -1) {
        addAttribute("VisibilityTimeout",
                     to_string(params.visibilityTimeout));
    }

    return performPost(std::move(queryParams), "",
                       "CreateQueueResponse/CreateQueueResult/QueueUrl");
}

void
SqsApi::
deleteQueue(const std::string & queueUri)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "DeleteQueue"});

    performGet(std::move(queryParams), getQueueResource(queueUri));
}

std::string
SqsApi::
getQueueUrl(const std::string & queueName,
            const std::string & ownerAccountId)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "GetQueueUrl"});
    queryParams.push_back({"QueueName", queueName});
    queryParams.push_back({"Version", "2012-11-05"});
    if (ownerAccountId != "")
        queryParams.push_back({"QueueOwnerAWSAccountId", ownerAccountId});

    return performGet(std::move(queryParams), "",
                      "GetQueueUrlResponse/GetQueueUrlResult/QueueUrl");
}

std::string
SqsApi::
sendMessage(const std::string & queueUri,
            const std::string & message,
            int timeoutSeconds,
            int delaySeconds)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "SendMessage"});
    queryParams.push_back({"Version", "2012-11-05"});
    queryParams.push_back({"MessageBody", message});

    return performPost(std::move(queryParams), getQueueResource(queueUri),
                       "SendMessageResponse/SendMessageResult/MD5OfMessageBody");
}

SqsApi::Message
SqsApi::
receiveMessage(const std::string & queueUri,
               int visibilityTimeout,
               int waitTimeSeconds)
{
    
    SqsApi::Message message;

    auto messages = receiveMessageBatch(queueUri, 1,
                                        visibilityTimeout, waitTimeSeconds);
    if (messages.size() > 0) {
        message = messages[0];
    }

    return message;
}

vector<SqsApi::Message>
SqsApi::
receiveMessageBatch(const std::string & queueUri,
                    int maxNumberOfMessages,
                    int visibilityTimeout,
                    int waitTimeSeconds)
{
    vector<SqsApi::Message> messages;

    RestParams queryParams;
    queryParams.push_back({"Action", "ReceiveMessage"});
    queryParams.push_back({"Version", "2012-11-05"});
    queryParams.push_back({"AttributeName.1", "All"});
    queryParams.push_back({"MaxNumberOfMessages",
                           to_string(maxNumberOfMessages)});
    if (visibilityTimeout != -1)
        queryParams.push_back({"VisibilityTimeout", to_string(visibilityTimeout)});
    if (waitTimeSeconds != -1)
        queryParams.push_back({"WaitTimeSeconds", to_string(waitTimeSeconds)});

    auto xml = performGet(std::move(queryParams), getQueueResource(queueUri));

    auto result = extractNode(xml->RootElement(), "ReceiveMessageResult");
    if (result->NoChildren()) {
        return messages;
    }

    messages.reserve(maxNumberOfMessages);

    auto messageNode = extractNode(result, "Message");
    while (messageNode) {
        messages.emplace_back(extractMessage(messageNode));
        messageNode = messageNode->NextSiblingElement();
    }

    return messages;
}

void
SqsApi::
deleteMessage(const std::string & queueUri,
              const std::string & receiptHandle)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "DeleteMessage"});
    queryParams.push_back({"ReceiptHandle", receiptHandle});
    queryParams.push_back({"Version", "2012-11-05"});

    performGet(std::move(queryParams), getQueueResource(queueUri));
}

void
SqsApi::
deleteMessageBatch(const std::string & queueUri,
                   const std::vector<std::string> & receiptHandles)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "DeleteMessageBatch"});
    queryParams.push_back({"Version", "2012-11-05"});

    int counter(1);
    for (const string & receiptHandle: receiptHandles) {
        string prefix = "DeleteMessageBatchRequestEntry." + to_string(counter);
        queryParams.push_back({prefix + ".Id", "msg" + to_string(counter)});
        queryParams.push_back({prefix + ".ReceiptHandle", receiptHandle});
        counter++;
    }

    performGet(std::move(queryParams), getQueueResource(queueUri));
}

void
SqsApi::
changeMessageVisibility(const std::string & queueUri,
                        const std::string & receiptHandle,
                        int visibilityTimeout)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "ChangeMessageVisibility"});
    queryParams.push_back({"Version", "2012-11-05"});
    queryParams.push_back({"ReceiptHandle", receiptHandle});
    queryParams.push_back({"VisibilityTimeout", to_string(visibilityTimeout)});

    auto xml = performGet(std::move(queryParams), getQueueResource(queueUri));

    xml->Print();
}

void
SqsApi::
changeMessageVisibilityBatch(const std::string & queueUri,
                             const std::vector<VisibilityPair> & visibilities)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "ChangeMessageVisibilityBatch"});
    queryParams.push_back({"Version", "2012-11-05"});

    int counter(1);
    for (const auto & pair: visibilities) {
        string prefix = ("ChangeMessageVisibilityBatchRequestEntry."
                         + to_string(counter));
        queryParams.push_back({prefix + ".Id", pair.receiptHandle});
        queryParams.push_back({prefix + ".VisibilityTimeout",
                               to_string(pair.visibilityTimeout)});
    }

    auto xml = performGet(std::move(queryParams), getQueueResource(queueUri));

    xml->Print();
}

void
SqsApi::
addPermission(const std::string & queueUri, const std::string & label,
              const vector<RightsPair> & rights)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "AddPermission"});
    queryParams.push_back({"Version", "2012-11-05"});
    queryParams.push_back({"Label", label});

    int counter(1);
    for (const RightsPair & pair: rights) {
        if (pair.rights == Rights::All) {
            queryParams.push_back({"AWSAccountId." + to_string(counter),
                                   pair.principal});
            queryParams.push_back({"ActionName." + to_string(counter),
                                   "*"});
            counter++;
        }
        else {
            Rights currentRights = pair.rights;
            for (int i = 0; currentRights != None && i < 5; i++) {
                Rights currentRight = static_cast<Rights>(1 << i);
                if (currentRights & currentRight) {
                    queryParams.push_back({"AWSAccountId." + to_string(counter),
                                           pair.principal});
                    queryParams.push_back({"ActionName." + to_string(counter),
                                           SqsApi::rightToString(currentRight)});
                    counter++;
                    currentRights = static_cast<Rights>(currentRights & ~currentRight);
                }
            }
        }
    }

    auto xml = performGet(std::move(queryParams), getQueueResource(queueUri));

    xml->Print();
}

void
SqsApi::
removePermission(const std::string & queueUri, const std::string & label)
{
    RestParams queryParams;
    queryParams.push_back({"Action", "RemovePermission"});
    queryParams.push_back({"Version", "2012-11-05"});
    queryParams.push_back({"Label", label});

    auto xml = performGet(std::move(queryParams), getQueueResource(queueUri));

    xml->Print();
}

std::string
SqsApi::
getQueueResource(const std::string & queueUri) const
{
    ExcAssert(!serviceUri.empty());

    if (queueUri.find(serviceUri) != 0)
        throw ML::Exception("unknown queue URI");
    string resource(queueUri, serviceUri.size());

    return resource;
}

std::string
SqsApi::
rightToString(enum SqsApi::Rights rights)
{
    switch (rights) {
    case SendMessage: return "SendMessage";
    case DeleteMessage: return "DeleteMessage";
    case ChangeMessageVisibility: return "ChangeMessageVisibility";
    case GetQueueAttributes: return "GetQueueAttributes";
    case GetQueueUrl: return "GetQueueUrl";
    case All: return "*";
    default:
        throw ML::Exception("unknown right");
    };
}


} // namespace Datacratic
