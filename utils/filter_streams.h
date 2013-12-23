/* filter_streams.h                                                -*- C++ -*-
   Jeremy Barnes, 12 March 2005
   Copyright (c) 2005 Jeremy Barnes.  All rights reserved.
   
   This file is part of "Jeremy's Machine Learning Library", copyright (c)
   1999-2005 Jeremy Barnes.
   
   This program is available under the GNU General Public License, the terms
   of which are given by the file "license.txt" in the top level directory of
   the source code distribution.  If this file is missing, you have no right
   to use the program; please contact the author.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   ---
   
   Streams that understand "-" syntax.
*/

#ifndef __utils__filter_streams_h__
#define __utils__filter_streams_h__

#include <iostream>
#include <fstream>
#include <memory>

namespace ML {


/*****************************************************************************/
/* FILTER OSTREAM                                                            */
/*****************************************************************************/

/** Ostream class that has the following features:
    - It has move semantics so can be passed by reference
    - It can add filters to compress / decompress
    - It can hook into other filesystems (eg s3, ...) based upon an
      extensible API.
*/

class filter_ostream : public std::ostream {
public:
    filter_ostream();
    filter_ostream(const std::string & uri,
                   std::ios_base::openmode mode = std::ios_base::out,
                   const std::string & compression = "",
                   int compressionLevel = -1);
    filter_ostream(int fd,
                   std::ios_base::openmode mode = std::ios_base::out,
                   const std::string & compression = "",
                   int compressionLevel = -1);

    filter_ostream(filter_ostream && other) noexcept;

    filter_ostream & operator = (filter_ostream && other);

    ~filter_ostream();

    void open(const std::string & uri,
              std::ios_base::openmode mode = std::ios_base::out,
              const std::string & compression = "",
              int level = -1);
    void open(int fd,
              std::ios_base::openmode mode = std::ios_base::out,
              const std::string & compression = "",
              int level = -1);

    void openFromStreambuf(std::streambuf * buf,
                           bool weOwnBuf,
                           const std::string & resource = "",
                           const std::string & compression = "",
                           int compressionLevel = -1);

    void close();

    std::string status() const;

private:
    std::unique_ptr<std::ostream> stream;
    std::unique_ptr<std::streambuf> sink;
};


/*****************************************************************************/
/* FILTER ISTREAM                                                            */
/*****************************************************************************/

class filter_istream : public std::istream {
public:
    filter_istream();
    filter_istream(const std::string & uri,
                   std::ios_base::openmode mode = std::ios_base::in,
                   const std::string & compression = "");

    filter_istream(filter_istream && other) noexcept;

    filter_istream & operator = (filter_istream && other);

    ~filter_istream();

    void open(const std::string & uri,
              std::ios_base::openmode mode = std::ios_base::in,
              const std::string & comparession = "");

    void openFromStreambuf(std::streambuf * buf,
                           bool weOwnBuf,
                           const std::string & resource = "",
                           const std::string & compression = "");

    void close();

private:
    std::unique_ptr<std::istream> stream;
    std::unique_ptr<std::streambuf> sink;
};


/*****************************************************************************/
/* REGISTRY                                                                  */
/*****************************************************************************/

typedef std::function<std::pair<std::streambuf *, bool>
                      (const std::string & scheme,
                       const std::string & resource,
                       std::ios_base::openmode mode)>
UriHandlerFunction;

void registerUriHandler(const std::string & scheme,
                        const UriHandlerFunction & handler);


} // namespace ML

#endif /* __utils__filter_streams_h__ */

