/*
 * Copyright (C) 2015 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "ignition/common/Console.hh"
#include "ignition/common/Exception.hh"

using namespace ignition;
using namespace common;
using namespace std;

//////////////////////////////////////////////////
Exception::Exception()
{
}

//////////////////////////////////////////////////
Exception::Exception(const char *_file, int _line, const std::string &_msg)
{
  this->file = _file;
  this->line = _line;
  this->str = _msg;
  this->Print();
}

//////////////////////////////////////////////////
Exception::~Exception()
{
}

//////////////////////////////////////////////////
void Exception::Print() const
{
  (ignition::common::Console::err(this->file, this->line)) << "EXCEPTION: "
      << *this << std::endl;
}

//////////////////////////////////////////////////
std::string Exception::ErrorFile() const
{
  return this->file;
}

//////////////////////////////////////////////////
std::string Exception::ErrorStr() const
{
  return this->str;
}

//////////////////////////////////////////////////
InternalError::InternalError()
{
}

//////////////////////////////////////////////////
InternalError::InternalError(const char *_file, int _line,
                             const std::string &_msg) :
  Exception(_file, _line, _msg)
{
}

//////////////////////////////////////////////////
InternalError::~InternalError()
{
}

//////////////////////////////////////////////////
AssertionInternalError::AssertionInternalError(
    const char * _file, int _line,
    const std::string &_expr,
    const std::string &_function,
    const std::string &_msg) :
  InternalError(_file, _line,
      "IGNITION ASSERTION                   \n" +
      _msg                               + "\n" +
      "In function       : " + _function + "\n" +
      "Assert expression : " + _expr     + "\n")
{
}

//////////////////////////////////////////////////
AssertionInternalError::~AssertionInternalError()
{
}
