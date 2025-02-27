/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PlatformUtilities.h"

#include <wtf/RetainPtr.h>
#include <wtf/StdLibExtras.h>
#include <wtf/UniqueArray.h>

namespace TestWebKitAPI {
namespace Util {

std::string toSTD(NSString *string)
{
    if (!string)
        return std::string();

    size_t bufferSize = [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    auto buffer = makeUniqueWithoutFastMallocCheck<char[]>(bufferSize);
    NSUInteger stringLength;
    [string getBytes:buffer.get() maxLength:bufferSize usedLength:&stringLength encoding:NSUTF8StringEncoding options:0 range:NSMakeRange(0, [string length]) remainingRange:0];
    return std::string(buffer.get(), stringLength);
}

bool jsonMatchesExpectedValues(NSString *jsonString, NSDictionary *expected)
{
    NSError *error = nil;
    id result = [NSJSONSerialization JSONObjectWithData:[jsonString dataUsingEncoding:NSUTF8StringEncoding] options:0 error:&error];
    if (error)
        NSLog(@"Encountered error: %@ while serializing JSON string.", error);
    return [expected isEqualToDictionary:result];
}

NSString * const TestPlugInClassNameParameter = @"TestPlugInPrincipalClassName";

} // namespace Util
} // namespace TestWebKitAPI
