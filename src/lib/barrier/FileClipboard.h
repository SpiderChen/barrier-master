/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#pragma once

#include "base/String.h"
#include "common/basic_types.h"
#include "common/stdvector.h"

typedef std::vector<String> FilePathList;

class FileClipboard {
public:
    static FilePathList split(const String& data);
    static String join(const FilePathList& paths);
    static UInt32 setupTransferInfo(const FilePathList& paths,
                                    FilePathList& transferablePaths,
                                    String& output);
    static String filenameFromPath(const String& path);

private:
    static bool getFileSize(const String& path, size_t& size);
};
