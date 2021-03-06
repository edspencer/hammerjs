/*
    Copyright (c) 2011 Sencha Inc.
    Copyright (c) 2010 Sencha Inc.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#include <v8.h>

#if defined(WIN32) || defined(_WIN32)
#define HAMMERJS_OS_WINDOWS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>
#include <iostream>
#include <sstream>

#if defined(HAMMERJS_OS_WINDOWS)
#include <windows.h>
#if !defined(PATH_MAX)
#define PATH_MAX MAX_PATH
#endif
#define PATH_SEPARATOR "\\"
#else // HAMMERJS_OS_WINDOWS
#include <dirent.h>
#include <unistd.h>
#define PATH_SEPARATOR "/"
#endif

using namespace v8;

static void CleanupStream(Persistent<Value>, void *data)
{
    delete reinterpret_cast<std::fstream*>(data);
}

static Handle<Value> fs_exists(const Arguments& args)
{
    HandleScope handle_scope;

    if (args.Length() != 1)
        return ThrowException(String::New("Exception: function fs.exists() accepts 1 argument"));

    String::Utf8Value fileName(args[0]);

#if defined(HAMMERJS_OS_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA attr;
    bool canStat = ::GetFileAttributesEx(*fileName, GetFileExInfoStandard, &attr) != 0;
#else
    struct stat statbuf;
    bool canStat = ::stat(*fileName, &statbuf) == 0;
#endif
    return Boolean::New(canStat);
}

static Handle<Value> fs_isDirectory(const Arguments& args)
{
    HandleScope handle_scope;

    if (args.Length() != 1)
        return ThrowException(String::New("Exception: fs.isDirectory() accepts 1 argument"));

    String::Utf8Value name(args[0]);

#if defined(HAMMERJS_OS_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!::GetFileAttributesEx(*name, GetFileExInfoStandard, &attr))
        return ThrowException(String::New("Exception: fs.isDirectory() can't access the directory"));

    return Boolean::New((attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
    struct stat statbuf;
    if (::stat(*name, &statbuf))
        return ThrowException(String::New("Exception: fs.isDirectory() can't access the directory"));

    return Boolean::New(S_ISDIR(statbuf.st_mode));
#endif
}

static Handle<Value> fs_isFile(const Arguments& args)
{
    HandleScope handle_scope;

    if (args.Length() != 1)
        return ThrowException(String::New("Exception: fs.isFile() accepts 1 argument"));

    String::Utf8Value name(args[0]);

#if defined(HAMMERJS_OS_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!::GetFileAttributesEx(*name, GetFileExInfoStandard, &attr))
        return ThrowException(String::New("Exception: fs.isFile() can't access the file"));

    return Boolean::New((attr.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY)) == 0);
#else
    struct stat statbuf;
    if (::stat(*name, &statbuf))
        return ThrowException(String::New("Exception: fs.isFile() can't access the file"));

    return Boolean::New(S_ISREG(statbuf.st_mode));
#endif
}

static Handle<Value> fs_makeDirectory(const Arguments& args)
{
    HandleScope handle_scope;

    if (args.Length() != 1)
        return ThrowException(String::New("Exception: function fs.makeDirectory() accepts 1 argument"));

    String::Utf8Value directoryName(args[0]);

#if defined(HAMMERJS_OS_WINDOWS)
    if (::CreateDirectory(*directoryName, NULL) == 0)
        return ThrowException(String::New("Exception: fs.makeDirectory() can't create the directory"));
#else
    if (::mkdir(*directoryName, 0777) != 0)
        return ThrowException(String::New("Exception: fs.makeDirectory() can't create the directory"));
#endif

    return Undefined();
}

static Handle<Value> fs_list(const Arguments& args)
{
    HandleScope handle_scope;

    if (args.Length() != 1)
        return ThrowException(String::New("Exception: fs.list() accepts 1 argument"));

    String::Utf8Value dirname(args[0]);

#if defined(HAMMERJS_OS_WINDOWS)
    char *search = new char[dirname.length() + 3];
    strcpy(search, *dirname);
    strcat(search, "\\*");
    WIN32_FIND_DATA entry;
    HANDLE dir = INVALID_HANDLE_VALUE;
    dir = FindFirstFile(search, &entry);
    if (dir == INVALID_HANDLE_VALUE)
        return ThrowException(String::New("Exception: fs.list() can't access the directory"));

    Handle<Array> entries = Array::New();
    int count = 0;
    do {
        if (strcmp(entry.cFileName, ".") && strcmp(entry.cFileName, "..")) {
            entries->Set(count++, String::New(entry.cFileName));
        }
    } while (FindNextFile(dir, &entry) != 0);
    FindClose(dir);

    return entries;
#else
    DIR *dir = opendir(*dirname);
    if (!dir)
        return ThrowException(String::New("Exception: fs.list() can't access the directory"));

    Handle<Array> entries = Array::New();
    int count = 0;
    struct dirent entry;
    struct dirent *ptr = NULL;
    ::readdir_r(dir, &entry, &ptr);
    while (ptr) {
        if (strcmp(entry.d_name, ".") && strcmp(entry.d_name, ".."))
            entries->Set(count++, String::New(entry.d_name));
        ::readdir_r(dir, &entry, &ptr);
    }
    ::closedir(dir);

    return entries;
#endif
}

static Handle<Value> fs_open(const Arguments& args)
{
    HandleScope handle_scope;

    if (args.Length() != 1 && args.Length() != 2)
        return ThrowException(String::New("Exception: function fs.open() accepts 1 or 2 arguments"));

    Handle<Context> context = Context::GetCurrent();
    Handle<Value> streamClass = context->Global()->Get(String::New("Stream"));
    Function *streamFunction = Function::Cast(*streamClass);

    Handle<Value> argv[2];
    argv[0] = args[0];
    if (args.Length() == 2)
        argv[1] = args[1];

    Handle<Object> result = streamFunction->NewInstance(args.Length(), argv);
    return result;
}

static Handle<Value> fs_workingDirectory(const Arguments& args)
{
    if (args.Length() != 0)
        return ThrowException(String::New("Exception: function fs.workingDirectory() accepts no argument"));

    char currentName[PATH_MAX + 1];
    currentName[0] = 0;
#if defined(HAMMERJS_OS_WINDOWS)
    DWORD len = ::GetCurrentDirectory(PATH_MAX, currentName);
    if (len == 0 || len > PATH_MAX)
        return ThrowException(String::New("Exception: function fs.workingDirectory() can not get current directory"));
    return String::New(currentName);
#else
    if (::getcwd(currentName, PATH_MAX))
        return String::New(currentName);
#endif

    return ThrowException(String::New("Exception: fs.workingDirectory() can't get current working directory"));
}

static Handle<Value> stream_constructor(const Arguments& args)
{
    HandleScope handle_scope;

    if (args.Length() != 1 && args.Length() != 2)
        return ThrowException(String::New("Exception: Stream constructor accepts 1 or 2 arguments"));

    String::Utf8Value name(args[0]);
    String::Utf8Value modes(args[1]);

    std::fstream::openmode mode = std::fstream::in;
    if (args.Length() == 2) {
        String::Utf8Value m(args[0]);
        const char* options = *modes;
        bool read = strchr(options, 'r');
        bool write = strchr(options, 'w');
        if (!read && !write)
            return ThrowException(String::New("Exception: Invalid open mode for Stream"));
        if (!read)
            mode = std::fstream::out;
        if (write)
            mode |= std::fstream::out;
    }

    std::fstream *data = new std::fstream;
    data->open(*name, mode);
    if (data->fail() || data->bad()) {
        delete data;
        return ThrowException(String::New("Exception: Can't open the file"));
    }

    args.This()->SetPointerInInternalField(0, data);

    Persistent<Object> persistent = Persistent<Object>::New(args.Holder());
    persistent.MakeWeak(data, CleanupStream);

    persistent->Set(String::New("name"), args[0]);

    return handle_scope.Close(persistent);
}

static Handle<Value> stream_close(const Arguments& args)
{
    if (args.Length() != 0)
        return ThrowException(String::New("Exception: Stream.close() accepts no argument"));

    void *data = args.This()->GetPointerFromInternalField(0);
    std::fstream *fs = reinterpret_cast<std::fstream*>(data);
    fs->close();

    return Undefined();
}

static Handle<Value> stream_flush(const Arguments& args)
{
    if (args.Length() != 0)
        return ThrowException(String::New("Exception: Stream.flush() accepts no argument"));

    void *data = args.This()->GetPointerFromInternalField(0);
    std::fstream *fs = reinterpret_cast<std::fstream*>(data);
    fs->flush();

    return args.This();
}

static Handle<Value> stream_next(const Arguments& args)
{
    if (args.Length() != 0)
        return ThrowException(String::New("Exception: Stream.next() accepts no argument"));

    void *data = args.This()->GetPointerFromInternalField(0);
    std::fstream *fs = reinterpret_cast<std::fstream*>(data);

    std::string buffer;
    std::getline(*fs, buffer);
    if (fs->eof())
        return ThrowException(String::New("Exception: Stream.next() reaches end of file"));

    return String::New(buffer.c_str());
}

static Handle<Value> stream_readLine(const Arguments& args)
{
    if (args.Length() != 0)
        return ThrowException(String::New("Exception: Stream.readLine() accepts no argument"));

    void *data = args.This()->GetPointerFromInternalField(0);
    std::fstream *fs = reinterpret_cast<std::fstream*>(data);

    if (fs->eof())
        return String::NewSymbol("");

    std::string buffer;
    std::getline(*fs, buffer);
    buffer.append("\n");

    return String::New(buffer.c_str());
}

static Handle<Value> stream_writeLine(const Arguments& args)
{
    if (args.Length() != 1)
        return ThrowException(String::New("Exception: Stream.writeLine() accepts 1 argument"));

    void *data = args.This()->GetPointerFromInternalField(0);
    std::fstream *fs = reinterpret_cast<std::fstream*>(data);

    String::Utf8Value line(args[0]);
    fs->write(*line, line.length());
    fs->put('\n');

    return args.This();
}

void setup_fs(Handle<Object> object, Handle<Array> args)
{
    // 'fs' object
    Handle<FunctionTemplate> fsObject = FunctionTemplate::New();
    fsObject->Set(String::New("pathSeparator"), String::New(PATH_SEPARATOR), ReadOnly);
    fsObject->Set(String::New("exists"), FunctionTemplate::New(fs_exists)->GetFunction());
    fsObject->Set(String::New("makeDirectory"), FunctionTemplate::New(fs_makeDirectory)->GetFunction());
    fsObject->Set(String::New("isDirectory"), FunctionTemplate::New(fs_isDirectory)->GetFunction());
    fsObject->Set(String::New("isFile"), FunctionTemplate::New(fs_isFile)->GetFunction());
    fsObject->Set(String::New("list"), FunctionTemplate::New(fs_list)->GetFunction());
    fsObject->Set(String::New("open"), FunctionTemplate::New(fs_open)->GetFunction());
    fsObject->Set(String::New("workingDirectory"), FunctionTemplate::New(fs_workingDirectory)->GetFunction());

    // 'Stream' class
    Handle<FunctionTemplate> streamClass = FunctionTemplate::New(stream_constructor);
    streamClass->SetClassName(String::New("Stream"));
    streamClass->InstanceTemplate()->SetInternalFieldCount(1);
    streamClass->InstanceTemplate()->Set(String::New("close"), FunctionTemplate::New(stream_close)->GetFunction());
    streamClass->InstanceTemplate()->Set(String::New("flush"), FunctionTemplate::New(stream_flush)->GetFunction());
    streamClass->InstanceTemplate()->Set(String::New("next"), FunctionTemplate::New(stream_next)->GetFunction());
    streamClass->InstanceTemplate()->Set(String::New("readLine"), FunctionTemplate::New(stream_readLine)->GetFunction());
    streamClass->InstanceTemplate()->Set(String::New("writeLine"), FunctionTemplate::New(stream_writeLine)->GetFunction());

    object->Set(String::New("fs"), fsObject->GetFunction());
    object->Set(String::New("Stream"), streamClass->GetFunction(), PropertyAttribute(ReadOnly | DontDelete));
}

