/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2013-2016 Symless Ltd.
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

#include "barrier/DragInformation.h"
#include "base/Log.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace std;

DragInformation::DragInformation() :
    m_filename(),
    m_filesize(0)
{
}

void
DragInformation::parseDragInfo(DragFileList& dragFileList, UInt32 fileNum, String data)
{
    size_t startPos = 0;
    size_t findResult1 = 0;
    size_t findResult2 = 0;
    dragFileList.clear();

    UInt32 index = 0;
    while (index < fileNum) {
        findResult1 = data.find(',', startPos);
        if (findResult1 == string::npos || findResult1 == startPos) {
            break;
        }

        findResult2 = data.find_last_of("/\\", findResult1);

        size_t filenameStart = findResult2 == string::npos ? startPos : findResult2 + 1;
        String filename = data.substr(filenameStart, findResult1 - filenameStart);
        if (filename.empty()) {
            break;
        }

        // set filename
        DragInformation di;
        di.setFilename(filename);
        dragFileList.push_back(di);
        startPos = findResult1 + 1;

        //set filesize
        findResult2 = data.find(',', startPos);
        if (findResult2 == string::npos || findResult2 == startPos) {
            dragFileList.pop_back();
            break;
        }

        String filesize = data.substr(startPos, findResult2 - startPos);
        size_t size = stringToNum(filesize);
        dragFileList.at(index).setFilesize(size);
        startPos = findResult2 + 1;

        ++index;
    }

    LOG((CLOG_DEBUG "drag info received, total drag file number: %i",
        dragFileList.size()));

    for (size_t i = 0; i < dragFileList.size(); ++i) {
        LOG((CLOG_DEBUG "dragging file %i name: %s",
            i + 1,
            dragFileList.at(i).getFilename().c_str()));
    }
}

String
DragInformation::getDragFileExtension(String filename)
{
    size_t findResult = string::npos;
    findResult = filename.find_last_of(".", filename.size());
    if (findResult != string::npos) {
        return filename.substr(findResult + 1, filename.size() - findResult - 1);
    }
    else {
        return "";
    }
}

int
DragInformation::setupDragInfo(DragFileList& fileList, String& output)
{
    int size = static_cast<int>(fileList.size());
    for (int i = 0; i < size; ++i) {
        output.append(fileList.at(i).getFilename());
        output.append(",");
        String filesize = getFileSize(fileList.at(i).getFilename());
        output.append(filesize);
        output.append(",");
    }
    return size;
}

bool
DragInformation::isFileValid(String filename)
{
    bool result = false;
    std::fstream file(filename.c_str(), ios::in|ios::binary);

    if (file.is_open()) {
        result = true;
    }

    file. close();

    return result;
}

size_t
DragInformation::stringToNum(String& str)
{
    istringstream iss(str.c_str());
    size_t size;
    iss >> size;
    return size;
}

String
DragInformation::getFileSize(String& filename)
{
    std::fstream file(filename.c_str(), ios::in|ios::binary);

    if (!file.is_open()) {
      throw std::runtime_error("failed to get file size");
    }

    // check file size
    file.seekg (0, std::ios::end);
    size_t size = (size_t)file.tellg();

    stringstream ss;
    ss << size;

    file. close();

    return ss.str();
}
