/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_StyleBackendType_h
#define mozilla_StyleBackendType_h

namespace mozilla {

/**
 * Enumeration that represents one of the two supported style system backends.
 */
enum class StyleBackendType : uint8_t
{
  Gecko = 1,
  Servo
};

} // namespace mozilla

#endif // mozilla_StyleBackendType_h
