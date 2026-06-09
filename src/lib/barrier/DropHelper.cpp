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
#include "io/filesystem.h"

#include <fstream>

#if SYSAPI_WIN32
#include "common/win32/encoding_utilities.h"
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

const char* const kFileClipboardCacheDir = "barrier-file-clipboard-cache";

String
fileClipboardCacheDir()
{
#if SYSAPI_WIN32
    DWORD size = GetCurrentDirectoryW(0, NULL);
    if (size == 0) {
        LOG((CLOG_ERR "file clipboard cache unavailable: GetCurrentDirectory failed %lu",
             GetLastError()));
        return "";
    }

    std::vector<WCHAR> cwd(size, 0);
    DWORD written = GetCurrentDirectoryW(size, cwd.data());
    if (written == 0 || written >= size) {
        LOG((CLOG_ERR "file clipboard cache unavailable: GetCurrentDirectory failed %lu",
             GetLastError()));
        return "";
    }

    std::vector<WCHAR> cacheName = utf8_to_win_char(kFileClipboardCacheDir);
    std::wstring cachePath(cwd.data());
    if (!cachePath.empty() && cachePath[cachePath.size() - 1] != L'\\') {
        cachePath.append(L"\\");
    }
    cachePath.append(cacheName.data());

    if (!CreateDirectoryW(cachePath.c_str(), NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        LOG((CLOG_ERR "file clipboard cache unavailable: CreateDirectory failed %lu",
             GetLastError()));
        return "";
    }

    return win_wchar_to_utf8(cachePath.c_str());
#else
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        LOG((CLOG_ERR "file clipboard cache unavailable: getcwd failed: %s",
             std::strerror(errno)));
        return "";
    }

    String cachePath(cwd);
    if (!cachePath.empty() && cachePath[cachePath.size() - 1] != '/') {
        cachePath.append("/");
    }
    cachePath.append(kFileClipboardCacheDir);

    if (mkdir(cachePath.c_str(), 0755) != 0 && errno != EEXIST) {
        LOG((CLOG_ERR "file clipboard cache unavailable: mkdir failed: %s",
             std::strerror(errno)));
        return "";
    }

    return cachePath;
#endif
}

} // namespace

String
DropHelper::writeToDir(const String& destination, DragFileList& fileList, String& data)
{
    LOG((CLOG_DEBUG "dropping file, files=%i target=%s", fileList.size(), destination.c_str()));

    if (!destination.empty() && fileList.size() > 0) {
        std::fstream file;
        String dropTarget = destination;
#ifdef SYSAPI_WIN32
        dropTarget.append("\\");
#else
        dropTarget.append("/");
#endif
        dropTarget.append(fileList.at(0).getFilename());
        barrier::open_utf8_path(file, dropTarget, std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            LOG((CLOG_ERR "drop file failed: can not open %s", dropTarget.c_str()));
            fileList.erase(fileList.begin());
            return "";
        }

        file.write(data.c_str(), data.size());
        file.close();

        LOG((CLOG_INFO "dropped file \"%s\" in \"%s\"", fileList.at(0).getFilename().c_str(), destination.c_str()));

        fileList.erase(fileList.begin());
        return dropTarget;
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
