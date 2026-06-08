/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

class MSWindowsClipboardFilesConverter : public IMSWindowsClipboardConverter {
public:
    MSWindowsClipboardFilesConverter();
    virtual ~MSWindowsClipboardFilesConverter();

    // IMSWindowsClipboardConverter overrides
    virtual IClipboard::EFormat getFormat() const;
    virtual UINT getWin32Format() const;
    virtual HANDLE fromIClipboard(const std::string& data) const;
    virtual std::string toIClipboard(HANDLE data) const;
};
