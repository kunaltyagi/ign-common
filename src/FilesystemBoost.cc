/*
 * Copyright 2002-2009, 2014 Beman Dawes
 * Copyright 2001 Dietmar Kuehl
 *
 * Distributed under the Boost Software License, Version 1.0.
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Most of this code was borrowed from Boost in
 * libs/filesystem/src/operations.cpp and
 * libs/filesystem/include/boost/filesystem/operations.hpp.
 */

#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <windows.h>
#include <winioctl.h>
#include <winnt.h>
#endif

#include "ignition/common/Filesystem.hh"

namespace ignition
{
  namespace common
  {
    /// \internal
    /// \brief Private data for the DirIter class.
    class DirIterPrivate
    {
      /// \def current
      /// \brief The current directory item.
      public: std::string current;

      /// \def dirname
      /// \brief The original path to the directory.
      public: std::string dirname;

      /// \def handle
      /// \brief Opaque handle for holding the directory iterator.
      public: void *handle;

      /// \def end
      /// \brief Private variable to indicate whether the iterator has reached
      ///        the end.
      public: bool end;
    };

#ifndef _WIN32

    static const char preferred_separator = '/';

    //////////////////////////////////////////////////
    bool exists(const std::string &_path)
    {
      struct stat path_stat;
      return ::stat(_path.c_str(), &path_stat) == 0;
    }

    //////////////////////////////////////////////////
    bool isDirectory(const std::string &_path)
    {
      struct stat path_stat;

      if (::stat(_path.c_str(), &path_stat) != 0)
      {
        return false;
      }

      return S_ISDIR(path_stat.st_mode);
    }

    //////////////////////////////////////////////////
    bool createDirectory(const std::string &_path)
    {
      return ::mkdir(_path.c_str(), S_IRWXU|S_IRWXG|S_IRWXO) == 0;
    }

    //////////////////////////////////////////////////
    std::string cwd()
    {
      std::string cur;
      // loop 'til buffer large enough
      for (int32_t path_max = 128;; path_max *= 2)
      {
        std::vector<char> buf(path_max);

        if (::getcwd(buf.data(), buf.size()) == 0)
        {
          if (errno != ERANGE)
          {
            break;
          }
        }
        else
        {
          char resolved[PATH_MAX];

          if (realpath(buf.data(), resolved) != nullptr)
          {
            cur = std::string(resolved);
          }
          break;
        }
      }
      return cur;
    }

    //////////////////////////////////////////////////
    DirIter::DirIter(const std::string &_in) : dataPtr(new DirIterPrivate)
    {
      this->dataPtr->dirname = _in;

      this->dataPtr->current = "";

      this->dataPtr->handle = opendir(_in.c_str());

      this->dataPtr->end = false;

      if (this->dataPtr->handle == nullptr)
      {
        this->dataPtr->end = true;
      }
      else
      {
        Next();
      }
    }

    //////////////////////////////////////////////////
    void DirIter::Next()
    {
      while (true)
      {
        struct dirent *entry =
          readdir(reinterpret_cast<DIR*>(this->dataPtr->handle)); // NOLINT
        if (!entry)
        {
          this->dataPtr->end = true;
          this->dataPtr->current = "";
          break;
        }

        if ((strcmp(entry->d_name, ".") != 0)
            && (strcmp(entry->d_name, "..") != 0))
        {
          this->dataPtr->current = std::string(entry->d_name);
          break;
        }
      }
    }

    //////////////////////////////////////////////////
    void DirIter::CloseHandle()
    {
      closedir(reinterpret_cast<DIR*>(this->dataPtr->handle));
    }

#else  // Windows

    static const char preferred_separator = '\\';

    //////////////////////////////////////////////////
    static bool not_found_error(int _errval)
    {
      return _errval == ERROR_FILE_NOT_FOUND
        || _errval == ERROR_PATH_NOT_FOUND
        || _errval == ERROR_INVALID_NAME  // "tools/src/:sys:stat.h", "//foo"
        || _errval == ERROR_INVALID_DRIVE  // USB card reader with no card
        || _errval == ERROR_NOT_READY  // CD/DVD drive with no disc inserted
        || _errval == ERROR_INVALID_PARAMETER  // ":sys:stat.h"
        || _errval == ERROR_BAD_PATHNAME  // "//nosuch" on Win64
        || _errval == ERROR_BAD_NETPATH;  // "//nosuch" on Win32
    }

    //////////////////////////////////////////////////
    static bool process_status_failure()
    {
      int errval(::GetLastError());

      if (not_found_error(errval))
      {
        return false;
      }
      else if ((errval == ERROR_SHARING_VIOLATION))
      {
        return true;  // odd, but this is what boost does
      }
      return false;
    }

    struct handle_wrapper
    {
      HANDLE handle;
      explicit handle_wrapper(HANDLE h)
        : handle(h) {}
      ~handle_wrapper()
      {
        if (handle != INVALID_HANDLE_VALUE)
        {
          ::CloseHandle(handle);
        }
      }
    };

    // REPARSE_DATA_BUFFER related definitions are in ntifs.h, which is part of
    // the Windows Device Driver Kit. Since it's inconvenient, the definitions
    // are provided here. http://msdn.microsoft.com/en-us/library/ms791514.aspx

    typedef struct _REPARSE_DATA_BUFFER {
      ULONG  ReparseTag;
      USHORT  ReparseDataLength;
      USHORT  Reserved;
      union {
        struct {
          USHORT  SubstituteNameOffset;
          USHORT  SubstituteNameLength;
          USHORT  PrintNameOffset;
          USHORT  PrintNameLength;
          ULONG  Flags;
          WCHAR  PathBuffer[1];
      /*  Example of distinction between substitute and print names:
            mklink /d ldrive c:\
            SubstituteName: c:\\??\
            PrintName: c:\
      */
        } SymbolicLinkReparseBuffer;
        struct {
          USHORT  SubstituteNameOffset;
          USHORT  SubstituteNameLength;
          USHORT  PrintNameOffset;
          USHORT  PrintNameLength;
          WCHAR  PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
          UCHAR  DataBuffer[1];
        } GenericReparseBuffer;
      };
    } REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

    //////////////////////////////////////////////////
    HANDLE create_file_handle(const std::string &_path, DWORD _dwDesiredAccess,
                              DWORD _dwShareMode,
                              LPSECURITY_ATTRIBUTES _lpSecurityAttributes,
                              DWORD _dwCreationDisposition,
                              DWORD _dwFlagsAndAttributes,
                              HANDLE _hTemplateFile)
    {
      return ::CreateFileA(_path.c_str(), _dwDesiredAccess,
                           _dwShareMode, _lpSecurityAttributes,
                           _dwCreationDisposition, _dwFlagsAndAttributes,
                           _hTemplateFile);
    }

#ifndef MAXIMUM_REPARSE_DATA_BUFFER_SIZE
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE  (16 * 1024)
#endif

    //////////////////////////////////////////////////
    bool is_reparse_point_a_symlink(const std::string &_path)
    {
      handle_wrapper h(create_file_handle(_path, FILE_READ_EA,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
          nullptr, OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
          nullptr));
      if (h.handle == INVALID_HANDLE_VALUE)
      {
        return false;
      }

      std::vector<wchar_t> buf(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);

      // Query the reparse data
      DWORD dwRetLen;
      BOOL result = ::DeviceIoControl(h.handle, FSCTL_GET_REPARSE_POINT,
          nullptr, 0, buf.data(), (DWORD)buf.size(), &dwRetLen, nullptr);
      if (!result)
      {
        return false;
      }

      return reinterpret_cast<const REPARSE_DATA_BUFFER*>(&buf[0])->ReparseTag
        == IO_REPARSE_TAG_SYMLINK
        // Issue 9016 asked that NTFS directory junctions be recognized as
        // directories.  That is equivalent to recognizing them as symlinks, and
        // then the normal symlink mechanism will recognize them as directories.
        //
        // Directory junctions are very similar to symlinks, but have some
        // performance and other advantages over symlinks. They can be created
        // from the command line with "mklink /j junction-name target-path".
        || reinterpret_cast<const REPARSE_DATA_BUFFER*>(&buf[0])->ReparseTag
        == IO_REPARSE_TAG_MOUNT_POINT;  // "directory junction" or "junction"
    }

    //////////////////////////////////////////////////
    bool internal_check_path(const std::string &_path, DWORD &attr)
    {
      attr = ::GetFileAttributesA(_path.c_str());
      if (attr == 0xFFFFFFFF)
      {
        return process_status_failure();
      }

      // reparse point handling;
      //   since GetFileAttributesW does not resolve symlinks, try to open file
      //   handle to discover if the file exists
      if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
      {
        handle_wrapper h(
          create_file_handle(
            _path,
            0,  // dwDesiredAccess; attributes only
            FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
            0,  // lpSecurityAttributes
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            0));  // hTemplateFile
        if (h.handle == INVALID_HANDLE_VALUE)
        {
          return process_status_failure();
        }

        if (!is_reparse_point_a_symlink(_path))
        {
          return true;
        }
      }

      return true;
    }

    //////////////////////////////////////////////////
    bool exists(const std::string &_path)
    {
      DWORD attr;
      return internal_check_path(_path, attr);
    }

    //////////////////////////////////////////////////
    bool isDirectory(const std::string &_path)
    {
      DWORD attr;

      if (internal_check_path(_path, attr))
      {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
      }

      return false;
    }

    //////////////////////////////////////////////////
    bool createDirectory(const std::string &_path)
    {
      return ::CreateDirectoryA(_path.c_str(), 0) != 0;
    }

    //////////////////////////////////////////////////
    std::string cwd()
    {
      DWORD sz;
      if ((sz = ::GetCurrentDirectoryA(0, nullptr)) == 0)
      {
        sz = 1;
      }

      std::vector<char> buf(sz);

      if (::GetCurrentDirectoryA(sz, buf.data()) == 0)
      {
        // error
        return std::string("");
      }
      else
      {
        return buf.data();
      }
    }

    //////////////////////////////////////////////////
    DirIter::DirIter(const std::string &_in) : dataPtr(new DirIterPrivate)
    {
      // use a form of search Sebastian Martel reports will work with Win98
      this->dataPtr->dirname = _in;

      this->dataPtr->current = "";

      this->dataPtr->end = false;

      if (_in.empty())
      {
        // To be compatible with Unix, if given an empty string, assume this
        // is the end.
        this->dataPtr->end = true;
        return;
      }

      std::string dirpath(_in);
      dirpath += (dirpath.empty()
                  || (dirpath[dirpath.size()-1] != '\\'
                      && dirpath[dirpath.size()-1] != '/'
                      && dirpath[dirpath.size()-1] != ':'))? "\\*" : "*";

      WIN32_FIND_DATAA data;
      this->dataPtr->handle = ::FindFirstFileA(dirpath.c_str(), &data);

      // Keep iterating until we hit the end, or get a value other than "." and
      // ".."
      while (this->dataPtr->handle != INVALID_HANDLE_VALUE &&
             (std::string(data.cFileName) == "." ||
              std::string(data.cFileName) == ".."))
      {
        this->dataPtr->handle = ::FindFirstFileA(dirpath.c_str(), &data);
      }

      if (this->dataPtr->handle == INVALID_HANDLE_VALUE)
      {
        this->dataPtr->handle = nullptr;  // signal eof
        this->dataPtr->end = true;
      }
      else
      {
        this->dataPtr->current = std::string(data.cFileName);
      }
    }

    //////////////////////////////////////////////////
    void DirIter::Next()
    {
      WIN32_FIND_DATAA data;
      if (::FindNextFileA(this->dataPtr->handle, &data) == 0)  // fails
      {
        this->dataPtr->end = true;
        this->dataPtr->current = "";
      }
      else
      {
        this->dataPtr->current = std::string(data.cFileName);
      }
    }

    //////////////////////////////////////////////////
    void DirIter::CloseHandle()
    {
      ::FindClose(this->dataPtr->handle);
    }

#endif  // _WIN32

    //////////////////////////////////////////////////
    const std::string separator(const std::string &_p)
    {
      return _p + preferred_separator;
    }

    //////////////////////////////////////////////////
    void changeFromUnixPath(std::string &_path)
    {
      // cppcheck-suppress knownConditionTrueFalse
      // cppcheck-suppress unmatchedSuppression
      if ('/' == preferred_separator)
        return;

      std::replace(_path.begin(), _path.end(), '/', preferred_separator);
    }

    //////////////////////////////////////////////////
    std::string copyFromUnixPath(const std::string &_path)
    {
      std::string copy = _path;
      changeFromUnixPath(copy);
      return copy;
    }

    //////////////////////////////////////////////////
    void changeToUnixPath(std::string &_path)
    {
      // cppcheck-suppress knownConditionTrueFalse
      // cppcheck-suppress unmatchedSuppression
      if ('/' == preferred_separator)
        return;

      std::replace(_path.begin(), _path.end(), preferred_separator, '/');
    }

    //////////////////////////////////////////////////
    std::string copyToUnixPath(const std::string &_path)
    {
      std::string copy = _path;
      changeToUnixPath(copy);
      return copy;
    }

    //////////////////////////////////////////////////
    std::string basename(const std::string &_path)
    {
      bool last_was_slash = false;
      std::string basename;

      basename.reserve(_path.length());

      for (size_t i = 0; i < _path.length(); ++i)
      {
        if (_path[i] == preferred_separator)
        {
          if (i == (_path.length() - 1))
          {
            // if this is the last character, then according to basename we
            // should return the portion of the path *before* the slash, i.e.
            // the one we were just building.  However, as a special case, if
            // basename is empty, we return just a "/".
            if (basename.size() == 0)
            {
              basename.push_back(preferred_separator);
            }
            break;
          }

          last_was_slash = true;
        }
        else
        {
          if (last_was_slash)
          {
            last_was_slash = false;
            basename.clear();
          }

          basename.push_back(_path[i]);
        }
      }

      return basename;
    }

    //////////////////////////////////////////////////
    DirIter::DirIter() : dataPtr(new DirIterPrivate)
    {
      this->dataPtr->current = "";

      this->dataPtr->dirname = "";

      this->dataPtr->handle = nullptr;

      this->dataPtr->end = true;
    }

    //////////////////////////////////////////////////
    std::string DirIter::operator*() const
    {
      return this->dataPtr->dirname + preferred_separator +
        this->dataPtr->current;
    }

    //////////////////////////////////////////////////
    // prefix operator; note that we don't support the postfix operator
    // because it is complicated to do so
    const DirIter& DirIter::operator++()
    {
      Next();
      return *this;
    }

    //////////////////////////////////////////////////
    bool DirIter::operator!=(const DirIter &_other) const
    {
      return this->dataPtr->end != _other.dataPtr->end;
    }

    //////////////////////////////////////////////////
    DirIter::~DirIter()
    {
      if (this->dataPtr->handle != nullptr)
      {
        CloseHandle();
        this->dataPtr->handle = nullptr;
      }
    }
  }
}
