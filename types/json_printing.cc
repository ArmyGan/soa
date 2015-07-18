/* json_printing.cc
   Jeremy Barnes, 8 March 2013
   Copyright (c) 2013 Datacratic Inc.  All rights reserved.

   Functionality to print JSON values.
*/

#include "jml/utils/exc_assert.h"

#include "json_printing.h"
#include "dtoa.h"

using namespace std;


namespace Datacratic {


void
StreamJsonPrintingContext::
writeStringUtf8(const Utf8String & s)
{
    stream << '\"';

    for (auto it = s.begin(), end = s.end();  it != end;  ++it) {
        int c = *it;
        if (c >= ' ' && c < 127 && c != '\"' && c != '\\')
            stream << (char)c;
        else {
            switch (c) {
            case '\t': stream << "\\t";  break;
            case '\n': stream << "\\n";  break;
            case '\r': stream << "\\r";  break;
            case '\b': stream << "\\b";  break;
            case '\f': stream << "\\f";  break;
            case '/':
            case '\\':
            case '\"': stream << '\\' << (char)c;  break;
            default:
                if (writeUtf8) {
                    char buf[4];
                    char * p = utf8::unchecked::append(c, buf);
                    stream.write(buf, p - buf);
                }
                else {
                    ExcAssert(c >= 0 && c < 65536);
                    stream << ML::format("\\u%04x", (unsigned)c);
                }
            }
        }
    }
    
    stream << '\"';
}


/*****************************************************************************/
/* STREAM JSON PRINTING CONTEXT                                              */
/*****************************************************************************/

StreamJsonPrintingContext::
StreamJsonPrintingContext(std::ostream & stream)
    : stream(stream), writeUtf8(true)
{
}

void
StreamJsonPrintingContext::
startObject()
{
    path.push_back(true /* isObject */);
    stream << "{";
}

void
StreamJsonPrintingContext::
startMember(const std::string & memberName)
{
    ExcAssert(path.back().isObject);
    //path.back().memberName = memberName;
    ++path.back().memberNum;
    if (path.back().memberNum != 0)
        stream << ",";
    stream << '\"';
    ML::jsonEscape(memberName, stream);
    stream << "\":";
}

void
StreamJsonPrintingContext::
endObject()
{
    ExcAssert(path.back().isObject);
    path.pop_back();
    stream << "}";
}

void
StreamJsonPrintingContext::
startArray(int knownSize)
{
    path.push_back(false /* isObject */);
    stream << "[";
}

void
StreamJsonPrintingContext::
newArrayElement()
{
    ExcAssert(!path.back().isObject);
    ++path.back().memberNum;
    if (path.back().memberNum != 0)
        stream << ",";
}

void
StreamJsonPrintingContext::
endArray()
{
    ExcAssert(!path.back().isObject);
    path.pop_back();
    stream << "]";
}
    
void
StreamJsonPrintingContext::
skip()
{
    stream << "null";
}

void
StreamJsonPrintingContext::
writeNull()
{
    stream << "null";
}

void
StreamJsonPrintingContext::
writeInt(int i)
{
    stream << i;
}

void
StreamJsonPrintingContext::
writeUnsignedInt(unsigned int i)
{
    stream << i;
}

void
StreamJsonPrintingContext::
writeLong(long int i)
{
    stream << i;
}

void
StreamJsonPrintingContext::
writeUnsignedLong(unsigned long int i)
{
    stream << i;
}

void
StreamJsonPrintingContext::
writeLongLong(long long int i)
{
    stream << i;
}

void
StreamJsonPrintingContext::
writeUnsignedLongLong(unsigned long long int i)
{
    stream << i;
}

void
StreamJsonPrintingContext::
writeFloat(float f)
{
    if (std::isfinite(f))
        stream << Datacratic::dtoa(f);
    else stream << "\"" << f << "\"";
}

void
StreamJsonPrintingContext::
writeDouble(double d)
{
    if (std::isfinite(d))
        stream << Datacratic::dtoa(d);
    else stream << "\"" << d << "\"";
}

void
StreamJsonPrintingContext::
writeString(const std::string & s)
{
    stream << '\"';
    ML::jsonEscape(s, stream);
    stream << '\"';
}

void
StreamJsonPrintingContext::
writeStringUtf8(const Utf8String & s);

void
StreamJsonPrintingContext::
writeJson(const Json::Value & val)
{
    stream << val.toStringNoNewLine();
}

void
StreamJsonPrintingContext::
writeBool(bool b)
{
    stream << (b ? "true": "false");
}



/*****************************************************************************/
/* STRING JSON PRINTING CONTEXT                                              */
/*****************************************************************************/

void
StringJsonPrintingContext::
write(char c)
{
    str += c;
}

void
StringJsonPrintingContext::
write(char c1, char c2)
{
    str += c1;
    str += c2;
}

void
StringJsonPrintingContext::
write(const char * s)
{
    str += s;
}

void
StringJsonPrintingContext::
write(const char * s, int len)
{
    str.append(s, len);
}

void
StringJsonPrintingContext::
write(const std::string & s)
{
    str.append(s);
}

void
StringJsonPrintingContext::
writeStringUtf8(const Utf8String & s)
{
    write('"');

    for (auto it = s.begin(), end = s.end();  it != end;  ++it) {
        int c = *it;
        if (c >= ' ' && c < 127 && c != '\"' && c != '\\')
            write((char)c);
        else {
            switch (c) {
            case '\t': write('\\', 't');  break;
            case '\n': write('\\', 'n');  break;
            case '\r': write('\\', 'r');  break;
            case '\b': write('\\', 'b');  break;
            case '\f': write('\\', 'f');  break;
            case '/':
            case '\\':
            case '\"': write('\\', (char)c);  break;
            default:
                if (writeUtf8) {
                    char buf[4];
                    char * p = utf8::unchecked::append(c, buf);
                    write(buf, p - buf);
                }
                else {
                    ExcAssert(c >= 0 && c < 65536);
                    write(ML::format("\\u%04x", (unsigned)c));
                }
            }
        }
    }
    
    write('"');
}

StringJsonPrintingContext::
StringJsonPrintingContext(std::string & str)
    : str(str), writeUtf8(true)
{
}

void
StringJsonPrintingContext::
startObject()
{
    path.push_back(true /* isObject */);
    write('{');
}

void
StringJsonPrintingContext::
startMember(const std::string & memberName)
{
    ExcAssert(path.back().isObject);
    //path.back().memberName = memberName;
    ++path.back().memberNum;
    if (path.back().memberNum != 0)
        write(',');
    write('\"');
    ML::jsonEscape(memberName, str);
    write('"', ':');
}

void
StringJsonPrintingContext::
endObject()
{
    ExcAssert(path.back().isObject);
    path.pop_back();
    write('}');
}

void
StringJsonPrintingContext::
startArray(int knownSize)
{
    path.push_back(false /* isObject */);
    write('[');
}

void
StringJsonPrintingContext::
newArrayElement()
{
    ExcAssert(!path.back().isObject);
    ++path.back().memberNum;
    if (path.back().memberNum != 0)
        write(',');
}

void
StringJsonPrintingContext::
endArray()
{
    ExcAssert(!path.back().isObject);
    path.pop_back();
    write(']');
}
    
void
StringJsonPrintingContext::
skip()
{
    write("null");
}

void
StringJsonPrintingContext::
writeNull()
{
    write("null");
}

void
StringJsonPrintingContext::
writeInt(int i)
{
    char buffer[128];
    int chars = sprintf(buffer, "%i", i);
    write(buffer, chars);
}

void
StringJsonPrintingContext::
writeUnsignedInt(unsigned int i)
{
    char buffer[128];
    int chars = sprintf(buffer, "%ui", i);
    write(buffer, chars);
}

void
StringJsonPrintingContext::
writeLong(long int i)
{
    char buffer[128];
    int chars = sprintf(buffer, "%li", i);
    write(buffer, chars);
}

void
StringJsonPrintingContext::
writeUnsignedLong(unsigned long int i)
{
    char buffer[128];
    int chars = sprintf(buffer, "%lui", i);
    write(buffer, chars);
}

void
StringJsonPrintingContext::
writeLongLong(long long int i)
{
    char buffer[128];
    int chars = sprintf(buffer, "%lli", i);
    write(buffer, chars);
}

void
StringJsonPrintingContext::
writeUnsignedLongLong(unsigned long long int i)
{
    char buffer[128];
    int chars = sprintf(buffer, "%llui", i);
    write(buffer, chars);
}

void
StringJsonPrintingContext::
writeFloat(float f)
{
    if (std::isfinite(f))
        str += Datacratic::dtoa(f);
    else {
        write('"');
        write(std::to_string(f));
        write('"');
    }
}

void
StringJsonPrintingContext::
writeDouble(double d)
{
    if (std::isfinite(d))
        str += Datacratic::dtoa(d);
    else {
        write('"');
        write(std::to_string(d));
        write('"');
    }
}

void
StringJsonPrintingContext::
writeString(const std::string & s)
{
    write('"');
    ML::jsonEscape(s, str);
    write('"');
}

void
StringJsonPrintingContext::
writeJson(const Json::Value & val)
{
    write(val.toStringNoNewLine());
}

void
StringJsonPrintingContext::
writeBool(bool b)
{
    write(b ? "true": "false");
}


/*****************************************************************************/
/* STRUCTURED JSON PRINTING CONTEXT                                          */
/*****************************************************************************/

StructuredJsonPrintingContext::
StructuredJsonPrintingContext()
    : current(&output)
{
}

void
StructuredJsonPrintingContext::
startObject()
{
    *current = Json::Value(Json::objectValue);
    path.push_back(current);
}

void
StructuredJsonPrintingContext::
startMember(const std::string & memberName)
{
    current = &(*path.back())[memberName];
}

void
StructuredJsonPrintingContext::
endObject()
{
    path.pop_back();
}

void
StructuredJsonPrintingContext::
startArray(int knownSize)
{
    *current = Json::Value(Json::arrayValue);
    path.push_back(current);
}

void
StructuredJsonPrintingContext::
newArrayElement()
{
    Json::Value & b = *path.back();
    current = &b[b.size()];
}

void
StructuredJsonPrintingContext::
endArray()
{
    path.pop_back();
}
    
void
StructuredJsonPrintingContext::
skip()
{
    *current = Json::Value();
}

void
StructuredJsonPrintingContext::
writeNull()
{
    *current = Json::Value();
}

void
StructuredJsonPrintingContext::
writeInt(int i)
{
    *current = i;
}

void
StructuredJsonPrintingContext::
writeUnsignedInt(unsigned int i)
{
    *current = i;
}

void
StructuredJsonPrintingContext::
writeLong(long int i)
{
    *current = i;
}

void
StructuredJsonPrintingContext::
writeUnsignedLong(unsigned long int i)
{
    *current = i;
}

void
StructuredJsonPrintingContext::
writeLongLong(long long int i)
{
    *current = i;
}

void
StructuredJsonPrintingContext::
writeUnsignedLongLong(unsigned long long int i)
{
    *current = i;
}

void
StructuredJsonPrintingContext::
writeFloat(float f)
{
    *current = f;
}

void
StructuredJsonPrintingContext::
writeDouble(double d)
{
    *current = d;
}

void
StructuredJsonPrintingContext::
writeString(const std::string & s)
{
    *current = s;
}

void
StructuredJsonPrintingContext::
writeStringUtf8(const Utf8String & s)
{
    *current = s;
}

void
StructuredJsonPrintingContext::
writeJson(const Json::Value & val)
{
    *current = val;
}

void
StructuredJsonPrintingContext::
writeBool(bool b)
{
    *current = b;
}


} // namespace Datacratic
