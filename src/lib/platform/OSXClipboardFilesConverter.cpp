/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#include "platform/OSXClipboardFilesConverter.h"

#include <CoreFoundation/CoreFoundation.h>
#include <vector>

namespace {

std::string
cfDataToString(CFDataRef data)
{
    if (data == NULL) {
        return "";
    }

    return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                       static_cast<size_t>(CFDataGetLength(data)));
}

std::vector<std::string>
splitLines(const std::string& data)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= data.size()) {
        size_t end = data.find('\n', start);
        std::string line = data.substr(
            start, end == std::string::npos ? end : end - start);
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

std::string
pathToFileURLData(const std::string& path)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(path.c_str()),
        static_cast<CFIndex>(path.size()),
        false);
    if (url == NULL) {
        return "";
    }

    CFDataRef data = CFURLCreateData(kCFAllocatorDefault, url,
                                     kCFStringEncodingUTF8, true);
    CFRelease(url);

    std::string result = cfDataToString(data);
    if (data != NULL) {
        CFRelease(data);
    }

    return result;
}

std::string
fileURLDataToPath(const std::string& data)
{
    CFURLRef url = CFURLCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(data.data()),
        static_cast<CFIndex>(data.size()),
        kCFStringEncodingUTF8,
        NULL);
    if (url == NULL) {
        return "";
    }

    CFIndex maxLength = CFURLGetBytes(url, NULL, 0) + 1;
    std::vector<UInt8> path(static_cast<size_t>(maxLength), 0);
    Boolean ok = CFURLGetFileSystemRepresentation(
        url, true, &path[0], static_cast<CFIndex>(path.size()));
    CFRelease(url);

    if (!ok) {
        return "";
    }

    return std::string(reinterpret_cast<const char*>(&path[0]));
}

} // namespace

OSXClipboardFilesConverter::OSXClipboardFilesConverter()
{
}

OSXClipboardFilesConverter::~OSXClipboardFilesConverter()
{
}

IClipboard::EFormat
OSXClipboardFilesConverter::getFormat() const
{
    return IClipboard::kFiles;
}

CFStringRef
OSXClipboardFilesConverter::getOSXFormat() const
{
    return CFSTR("public.file-url");
}

std::string
OSXClipboardFilesConverter::fromIClipboard(const std::string& data) const
{
    std::vector<std::string> paths = splitLines(data);
    std::string result;

    for (std::vector<std::string>::const_iterator index = paths.begin();
         index != paths.end(); ++index) {
        std::string urlData = pathToFileURLData(*index);
        if (urlData.empty()) {
            continue;
        }

        if (!result.empty()) {
            result.push_back('\n');
        }
        result.append(urlData);
    }

    return result;
}

std::string
OSXClipboardFilesConverter::toIClipboard(const std::string& data) const
{
    return fileURLDataToPath(data);
}
