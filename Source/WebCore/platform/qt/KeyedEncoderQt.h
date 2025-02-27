/*
 * Copyright (C) 2016 Konstantin Tokarev <annulen@yandex.ru>
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

#ifndef KeyedEncoderQt_h
#define KeyedEncoderQt_h

#include "KeyedCoding.h"

#include <QDataStream>
#include <QVariantHash>
#include <QVariantList>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class KeyedEncoderQt final : public KeyedEncoder {
public:
    KeyedEncoderQt();
    RefPtr<WebCore::SharedBuffer> finishEncoding() override;

    QVariantMap& toMap();
    void encodeVariant(const String& key, const QVariant&);

public:
    void encodeBytes(const String& key, const uint8_t*, size_t) override;
    void encodeBool(const String& key, bool) override;
    void encodeUInt32(const String& key, uint32_t) override;
    void encodeUInt64(const String& key, uint64_t) override;
    void encodeInt32(const String& key, int32_t) override;
    void encodeInt64(const String& key, int64_t) override;
    void encodeFloat(const String& key, float) override;
    void encodeDouble(const String& key, double) override;
    void encodeString(const String& key, const String&) override;

private:
    void beginObject(const String& key) override;
    void endObject() override;

    void beginArray(const String& key) override;
    void beginArrayElement() override;
    void endArrayElement() override;
    void endArray() override;

    QVariantMap& currentObject() { return m_objectStack.last().second; }
    QVariantList& currentArray() { return m_arrayStack.last().second; }

    Vector<std::pair<QString, QVariantMap>, 16> m_objectStack;
    Vector<std::pair<QString, QVariantList>, 16> m_arrayStack;
};

} // namespace WebCore

#endif // KeyedEncoderQt_h
