/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#pragma once

#include "platform/OSXClipboard.h"

//! Convert to/from macOS Finder file URL pasteboard data.
class OSXClipboardFilesConverter : public IOSXClipboardConverter {
public:
    OSXClipboardFilesConverter();
    virtual ~OSXClipboardFilesConverter();

    // IOSXClipboardConverter overrides
    virtual IClipboard::EFormat getFormat() const;
    virtual CFStringRef getOSXFormat() const;
    virtual std::string fromIClipboard(const std::string&) const;
    virtual std::string toIClipboard(const std::string&) const;
};
