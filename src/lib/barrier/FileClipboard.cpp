/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#include "barrier/FileClipboard.h"

#include "barrier/Clipboard.h"
#include "base/Log.h"
#include "io/filesystem.h"

#include <fstream>
#include <sstream>

FilePathList
FileClipboard::split(const String& data)
{
    FilePathList paths;
    size_t start = 0;
    while (start <= data.size()) {
        size_t end = data.find('\n', start);
        String path = data.substr(start, end == String::npos ? end : end - start);
        if (!path.empty()) {
            paths.push_back(path);
        }
        if (end == String::npos) {
            break;
        }
        start = end + 1;
    }
    return paths;
}

String
FileClipboard::join(const FilePathList& paths)
{
    String data;
    for (FilePathList::const_iterator index = paths.begin();
         index != paths.end(); ++index) {
        if (!data.empty()) {
            data.push_back('\n');
        }
        data.append(*index);
    }
    return data;
}

String
FileClipboard::marshallWithoutFiles(const IClipboard* clipboard)
{
    Clipboard filtered;
    if (clipboard == NULL || !clipboard->open(0)) {
        return filtered.marshall();
    }

    if (filtered.open(clipboard->getTime())) {
        filtered.empty();
        for (UInt32 format = 0; format != IClipboard::kNumFormats; ++format) {
            IClipboard::EFormat clipboardFormat =
                static_cast<IClipboard::EFormat>(format);
            if (clipboardFormat != IClipboard::kFiles &&
                clipboard->has(clipboardFormat)) {
                filtered.add(clipboardFormat, clipboard->get(clipboardFormat));
            }
        }
        filtered.close();
    }
    clipboard->close();

    return filtered.marshall();
}

UInt32
FileClipboard::setupTransferInfo(const FilePathList& paths,
                                 FilePathList& transferablePaths,
                                 String& output)
{
    transferablePaths.clear();
    output.clear();

    for (FilePathList::const_iterator index = paths.begin();
         index != paths.end(); ++index) {
        size_t size = 0;
        if (!getFileSize(*index, size)) {
            LOG((CLOG_WARN "skipping clipboard file that cannot be read: %s", index->c_str()));
            continue;
        }

        transferablePaths.push_back(*index);
        output.append(*index);
        output.append(",");

        std::ostringstream stream;
        stream << size;
        output.append(stream.str());
        output.append(",");
    }

    return static_cast<UInt32>(transferablePaths.size());
}

String
FileClipboard::filenameFromPath(const String& path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash == String::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

bool
FileClipboard::getFileSize(const String& path, size_t& size)
{
    std::fstream file;
    barrier::open_utf8_path(file, path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, std::ios::end);
    size = static_cast<size_t>(file.tellg());
    file.close();
    return true;
}
