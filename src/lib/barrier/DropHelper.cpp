/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2014-2016 Symless Ltd.
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "barrier/DropHelper.h"

#include "base/Log.h"
#include "common/DataDirectories.h"
#include "io/filesystem.h"

#include <exception>
#include <fstream>

namespace {

const char* const kFileClipboardCacheDir = "barrier-file-clipboard-cache";

String
fileClipboardCacheDir()
{
    barrier::fs::path profileDir = barrier::DataDirectories::profile();
    if (profileDir.empty()) {
        LOG((CLOG_ERR "file clipboard cache unavailable: profile directory is empty"));
        return "";
    }

    barrier::fs::path cachePath = profileDir / kFileClipboardCacheDir;
    try {
        barrier::fs::create_directories(cachePath);
    }
    catch (const std::exception& error) {
        LOG((CLOG_ERR "file clipboard cache unavailable: %s", error.what()));
        return "";
    }

    return cachePath.u8string();
}

} // namespace

String
DropHelper::writeToDir(const String& destination, DragFileList& fileList, String& data)
{
    LOG((CLOG_DEBUG "dropping file, files=%i target=%s", fileList.size(), destination.c_str()));

    if (!destination.empty() && fileList.size() > 0) {
        std::fstream file;
        barrier::fs::path dropTarget =
            barrier::fs::u8path(destination) /
            barrier::fs::u8path(fileList.at(0).getFilename());
        barrier::open_utf8_path(file, dropTarget, std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            LOG((CLOG_ERR "drop file failed: can not open %s", dropTarget.u8string().c_str()));
            fileList.erase(fileList.begin());
            return "";
        }

        file.write(data.c_str(), data.size());
        file.close();

        LOG((CLOG_INFO "dropped file \"%s\" in \"%s\"", fileList.at(0).getFilename().c_str(), destination.c_str()));

        fileList.erase(fileList.begin());
        return dropTarget.u8string();
    }
    else {
        LOG((CLOG_ERR "drop file failed: drop target is empty"));
        fileList.clear();
    }

    return "";
}

String
DropHelper::writeToFileClipboardCache(DragFileList& fileList, String& data)
{
    return writeToDir(fileClipboardCacheDir(), fileList, data);
}
