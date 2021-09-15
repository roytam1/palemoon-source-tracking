/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ChaosMode.h"

namespace mozilla {

namespace detail {

Atomic<uint32_t> gChaosModeCounter(0);
ChaosFeature gChaosFeatures = None;

} /* namespace detail */
} /* namespace mozilla */
