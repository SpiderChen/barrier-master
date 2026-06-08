/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2004 Chris Schoeneman
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

#include "platform/OSXClipboard.h"

#include "barrier/Clipboard.h"
#include "platform/OSXClipboardUTF16Converter.h"
#include "platform/OSXClipboardTextConverter.h"
#include "platform/OSXClipboardBMPConverter.h"
#include "platform/OSXClipboardHTMLConverter.h"
#include "platform/OSXClipboardFilesConverter.h"
#include "base/Log.h"
#include "arch/XArch.h"

#include <cstdint>
#include <vector>

namespace {

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

} // namespace

//
// OSXClipboard
//

OSXClipboard::OSXClipboard() :
    m_time(0),
    m_pboard(NULL)
{
    m_converters.push_back(new OSXClipboardHTMLConverter);
    m_converters.push_back(new OSXClipboardBMPConverter);
    m_converters.push_back(new OSXClipboardUTF16Converter);
    m_converters.push_back(new OSXClipboardFilesConverter);
    m_converters.push_back(new OSXClipboardTextConverter);



    OSStatus createErr = PasteboardCreate(kPasteboardClipboard, &m_pboard);
    if (createErr != noErr) {
        LOG((CLOG_DEBUG "failed to create clipboard reference: error %i", createErr));
        LOG((CLOG_ERR "unable to connect to pasteboard, clipboard sharing disabled", createErr));
        m_pboard = NULL;
        return;

    }

    OSStatus syncErr = PasteboardSynchronize(m_pboard);
    if (syncErr != noErr) {
        LOG((CLOG_DEBUG "failed to synchronize clipboard: error %i", syncErr));
    }
}

OSXClipboard::~OSXClipboard()
{
    clearConverters();
}

    bool
OSXClipboard::empty()
{
    LOG((CLOG_DEBUG "emptying clipboard"));
    if (m_pboard == NULL)
        return false;

    OSStatus err = PasteboardClear(m_pboard);
    if (err != noErr) {
        LOG((CLOG_DEBUG "failed to clear clipboard: error %i", err));
        return false;
    }

    return true;
}

    bool
OSXClipboard::synchronize()
{
    if (m_pboard == NULL)
        return false;

    PasteboardSyncFlags flags = PasteboardSynchronize(m_pboard);
    LOG((CLOG_DEBUG2 "flags: %x", flags));

    if (flags & kPasteboardModified) {
        return true;
    }
    return false;
}

void OSXClipboard::add(EFormat format, const std::string& data)
{
    if (m_pboard == NULL)
        return;

    LOG((CLOG_DEBUG "add %d bytes to clipboard format: %d", data.size(), format));
    if (format == IClipboard::kText) {
        LOG((CLOG_DEBUG " format of data to be added to clipboard was kText"));
    }
    else if (format == IClipboard::kBitmap) {
        LOG((CLOG_DEBUG " format of data to be added to clipboard was kBitmap"));
    }
    else if (format == IClipboard::kHTML) {
        LOG((CLOG_DEBUG " format of data to be added to clipboard was kHTML"));
    }

    for (ConverterList::const_iterator index = m_converters.begin();
            index != m_converters.end(); ++index) {

        IOSXClipboardConverter* converter = *index;

        // skip converters for other formats
        if (converter->getFormat() == format) {
            std::string osXData = converter->fromIClipboard(data);
            CFStringRef flavorType = converter->getOSXFormat();

            if (format == IClipboard::kFiles) {
                std::vector<std::string> fileURLs = splitLines(osXData);
                uintptr_t itemIndex = 1;
                for (std::vector<std::string>::const_iterator file = fileURLs.begin();
                     file != fileURLs.end(); ++file) {
                    CFDataRef dataRef = CFDataCreate(
                        kCFAllocatorDefault,
                        reinterpret_cast<const UInt8*>(file->data()),
                        static_cast<CFIndex>(file->size()));
                    if (dataRef == NULL) {
                        continue;
                    }

                    PasteboardItemID itemID =
                        reinterpret_cast<PasteboardItemID>(itemIndex++);
                    OSStatus err = PasteboardPutItemFlavor(
                        m_pboard,
                        itemID,
                        flavorType,
                        dataRef,
                        kPasteboardFlavorNoFlags);
                    if (err != noErr) {
                        LOG((CLOG_DEBUG "failed to add file URL to clipboard: error %i", err));
                    }
                    CFRelease(dataRef);
                }
            }
            else {
                CFDataRef dataRef = CFDataCreate(kCFAllocatorDefault, (UInt8 *)osXData.data(), osXData.size());
                PasteboardItemID itemID = 0;

                PasteboardPutItemFlavor(
                    m_pboard,
                    itemID,
                    flavorType,
                    dataRef,
                    kPasteboardFlavorNoFlags);

                if (dataRef != NULL) {
                    CFRelease(dataRef);
                }
            }

            LOG((CLOG_DEBUG "added %d bytes to clipboard format: %d", data.size(), format));
        }

    }
}

bool
OSXClipboard::open(Time time) const
{
    if (m_pboard == NULL)
        return false;

    LOG((CLOG_DEBUG "opening clipboard"));
    m_time = time;
    return true;
}

void
OSXClipboard::close() const
{
    LOG((CLOG_DEBUG "closing clipboard"));
    /* not needed */
}

IClipboard::Time
OSXClipboard::getTime() const
{
    return m_time;
}

bool
OSXClipboard::has(EFormat format) const
{
    if (m_pboard == NULL)
        return false;

    for (ConverterList::const_iterator index = m_converters.begin();
            index != m_converters.end(); ++index) {
        IOSXClipboardConverter* converter = *index;
        if (converter->getFormat() == format) {
            PasteboardFlavorFlags flags;
            CFStringRef type = converter->getOSXFormat();

            if (format == IClipboard::kFiles) {
                ItemCount itemCount = 0;
                if (PasteboardGetItemCount(m_pboard, &itemCount) != noErr) {
                    return false;
                }

                for (ItemCount itemIndex = 1; itemIndex <= itemCount; ++itemIndex) {
                    PasteboardItemID item;
                    if (PasteboardGetItemIdentifier(m_pboard, itemIndex, &item) != noErr) {
                        continue;
                    }
                    if (PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags) == noErr) {
                        return true;
                    }
                }

                return false;
            }

            PasteboardItemID item;
            if (PasteboardGetItemIdentifier(m_pboard, (CFIndex) 1, &item) != noErr) {
                return false;
            }

            if (PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags) == noErr) {
                return true;
            }
        }
    }

    return false;
}

std::string OSXClipboard::get(EFormat format) const
{
    CFStringRef type;
    PasteboardItemID item = 0;
    std::string result;

    if (m_pboard == NULL)
        return result;

    if (format != IClipboard::kFiles &&
        PasteboardGetItemIdentifier(m_pboard, (CFIndex) 1, &item) != noErr) {
        return result;
    }

    // find the converter for the first clipboard format we can handle
    IOSXClipboardConverter* converter = NULL;
    for (ConverterList::const_iterator index = m_converters.begin();
            index != m_converters.end(); ++index) {
        converter = *index;

        PasteboardFlavorFlags flags;
        type = converter->getOSXFormat();

        if (converter->getFormat() == format) {
            if (format == IClipboard::kFiles ||
                PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags) == noErr) {
                break;
            }
        }
        converter = NULL;
    }

    // if no converter then we don't recognize any formats
    if (converter == NULL) {
        LOG((CLOG_DEBUG "Unable to find converter for data"));
        return result;
    }

    if (format == IClipboard::kFiles) {
        ItemCount itemCount = 0;
        if (PasteboardGetItemCount(m_pboard, &itemCount) != noErr) {
            return result;
        }

        type = converter->getOSXFormat();
        for (ItemCount itemIndex = 1; itemIndex <= itemCount; ++itemIndex) {
            PasteboardItemID item;
            if (PasteboardGetItemIdentifier(m_pboard, itemIndex, &item) != noErr) {
                continue;
            }

            PasteboardFlavorFlags flags;
            if (PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags) != noErr) {
                continue;
            }

            CFDataRef buffer = NULL;
            if (PasteboardCopyItemFlavorData(m_pboard, item, type, &buffer) != noErr ||
                buffer == NULL) {
                continue;
            }

            std::string osXData(
                reinterpret_cast<const char*>(CFDataGetBytePtr(buffer)),
                static_cast<size_t>(CFDataGetLength(buffer)));
            CFRelease(buffer);

            std::string path = converter->toIClipboard(osXData);
            if (path.empty()) {
                continue;
            }

            if (!result.empty()) {
                result.push_back('\n');
            }
            result.append(path);
        }

        return result;
    }

    // get the clipboard data.
    CFDataRef buffer = NULL;
    try {
        OSStatus err = PasteboardCopyItemFlavorData(m_pboard, item, type, &buffer);

        if (err != noErr) {
            throw err;
        }

        result = std::string((char *) CFDataGetBytePtr(buffer), CFDataGetLength(buffer));
    }
    catch (OSStatus err) {
        LOG((CLOG_DEBUG "exception thrown in OSXClipboard::get MacError (%d)", err));
    }
    catch (...) {
        LOG((CLOG_DEBUG "unknown exception in OSXClipboard::get"));
        RETHROW_XTHREAD
    }

    if (buffer != NULL)
        CFRelease(buffer);

    return converter->toIClipboard(result);
}

    void
OSXClipboard::clearConverters()
{
    if (m_pboard == NULL)
        return;

    for (ConverterList::iterator index = m_converters.begin();
            index != m_converters.end(); ++index) {
        delete *index;
    }
    m_converters.clear();
}
