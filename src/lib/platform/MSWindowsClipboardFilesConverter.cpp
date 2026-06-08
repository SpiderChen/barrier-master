/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#include "platform/MSWindowsClipboardFilesConverter.h"

#include "common/win32/encoding_utilities.h"

#include <shellapi.h>
#include <shlobj.h>
#include <vector>

MSWindowsClipboardFilesConverter::MSWindowsClipboardFilesConverter()
{
}

MSWindowsClipboardFilesConverter::~MSWindowsClipboardFilesConverter()
{
}

IClipboard::EFormat
MSWindowsClipboardFilesConverter::getFormat() const
{
    return IClipboard::kFiles;
}

UINT
MSWindowsClipboardFilesConverter::getWin32Format() const
{
    return CF_HDROP;
}

HANDLE
MSWindowsClipboardFilesConverter::fromIClipboard(const std::string& data) const
{
    std::vector<WCHAR> files;
    size_t start = 0;
    while (start <= data.size()) {
        size_t end = data.find('\n', start);
        std::string path = data.substr(start, end == std::string::npos ? end : end - start);
        if (!path.empty()) {
            std::vector<WCHAR> widePath = utf8_to_win_char(path);
            files.insert(files.end(), widePath.begin(), widePath.end() - 1);
            files.push_back(L'\0');
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    files.push_back(L'\0');

    const SIZE_T bytes = sizeof(DROPFILES) + files.size() * sizeof(WCHAR);
    HGLOBAL result = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes);
    if (result == NULL) {
        return NULL;
    }

    DROPFILES* dropFiles = static_cast<DROPFILES*>(GlobalLock(result));
    if (dropFiles == NULL) {
        GlobalFree(result);
        return NULL;
    }

    dropFiles->pFiles = sizeof(DROPFILES);
    dropFiles->pt.x = 0;
    dropFiles->pt.y = 0;
    dropFiles->fNC = FALSE;
    dropFiles->fWide = TRUE;
    memcpy(reinterpret_cast<char*>(dropFiles) + sizeof(DROPFILES),
           &files[0], files.size() * sizeof(WCHAR));

    GlobalUnlock(result);
    return result;
}

std::string
MSWindowsClipboardFilesConverter::toIClipboard(HANDLE data) const
{
    HDROP drop = static_cast<HDROP>(data);
    const UINT count = DragQueryFileW(drop, 0xffffffff, NULL, 0);
    std::string result;

    for (UINT index = 0; index < count; ++index) {
        const UINT length = DragQueryFileW(drop, index, NULL, 0);
        std::vector<WCHAR> path(length + 1, L'\0');
        if (DragQueryFileW(drop, index, &path[0], length + 1) == 0) {
            continue;
        }

        if (!result.empty()) {
            result.push_back('\n');
        }
        result.append(win_wchar_to_utf8(&path[0]));
    }

    return result;
}
