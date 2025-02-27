/*
 * Copyright (C) 2009-2019 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "JSONObject.h"

#include "ArrayConstructor.h"
#include "BigIntObject.h"
#include "BooleanObject.h"
#include "Error.h"
#include "ExceptionHelpers.h"
#include "JSArray.h"
#include "JSArrayInlines.h"
#include "JSGlobalObject.h"
#include "LiteralParser.h"
#include "Lookup.h"
#include "ObjectConstructor.h"
#include "JSCInlines.h"
#include "PropertyNameArray.h"
#include <wtf/MathExtras.h>
#include <wtf/text/StringBuilder.h>

namespace JSC {

STATIC_ASSERT_IS_TRIVIALLY_DESTRUCTIBLE(JSONObject);

EncodedJSValue JSC_HOST_CALL JSONProtoFuncParse(JSGlobalObject*, CallFrame*);
EncodedJSValue JSC_HOST_CALL JSONProtoFuncStringify(JSGlobalObject*, CallFrame*);

}

#include "JSONObject.lut.h"

namespace JSC {

JSONObject::JSONObject(VM& vm, Structure* structure)
    : JSNonFinalObject(vm, structure)
{
}

void JSONObject::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    ASSERT(inherits(vm, info()));

    putDirectWithoutTransition(vm, vm.propertyNames->toStringTagSymbol, jsString(vm, "JSON"), PropertyAttribute::DontEnum | PropertyAttribute::ReadOnly);
}

// PropertyNameForFunctionCall objects must be on the stack, since the JSValue that they create is not marked.
class PropertyNameForFunctionCall {
public:
    PropertyNameForFunctionCall(const Identifier&);
    PropertyNameForFunctionCall(unsigned);

    JSValue value(ExecState*) const;

private:
    const Identifier* m_identifier;
    unsigned m_number;
    mutable JSValue m_value;
};

class Stringifier {
    WTF_MAKE_NONCOPYABLE(Stringifier);
    WTF_FORBID_HEAP_ALLOCATION;
public:
    Stringifier(ExecState*, JSValue replacer, JSValue space);
    JSValue stringify(JSValue);

private:
    class Holder {
    public:
        enum RootHolderTag { RootHolder };
        Holder(ExecState*, JSObject*);
        Holder(RootHolderTag, JSObject*);

        JSObject* object() const { return m_object; }
        bool isArray() const { return m_isArray; }

        bool appendNextProperty(Stringifier&, StringBuilder&);

    private:
        JSObject* m_object;
        const bool m_isJSArray;
        const bool m_isArray;
        unsigned m_index { 0 };
        unsigned m_size { 0 };
        RefPtr<PropertyNameArrayData> m_propertyNames;
    };

    friend class Holder;

    JSValue toJSON(JSValue, const PropertyNameForFunctionCall&);
    JSValue toJSONImpl(VM&, JSValue, JSValue toJSONFunction, const PropertyNameForFunctionCall&);

    enum StringifyResult { StringifyFailed, StringifySucceeded, StringifyFailedDueToUndefinedOrSymbolValue };
    StringifyResult appendStringifiedValue(StringBuilder&, JSValue, const Holder&, const PropertyNameForFunctionCall&);

    bool willIndent() const;
    void indent();
    void unindent();
    void startNewLine(StringBuilder&) const;
    bool isCallableReplacer() const { return m_replacerCallType != CallType::None; }

    ExecState* const m_exec;
    JSValue m_replacer;
    bool m_usingArrayReplacer { false };
    PropertyNameArray m_arrayReplacerPropertyNames;
    CallType m_replacerCallType { CallType::None };
    CallData m_replacerCallData;
    String m_gap;

    MarkedArgumentBuffer m_objectStack;
    Vector<Holder, 16, UnsafeVectorOverflow> m_holderStack;
    String m_repeatedGap;
    String m_indent;
};

// ------------------------------ helper functions --------------------------------

static inline JSValue unwrapBoxedPrimitive(ExecState* exec, JSValue value)
{
    VM& vm = exec->vm();
    if (!value.isObject())
        return value;
    JSObject* object = asObject(value);
    if (object->inherits<NumberObject>(vm))
        return jsNumber(object->toNumber(exec));
    if (object->inherits<StringObject>(vm))
        return object->toString(exec);
    if (object->inherits<BooleanObject>(vm) || object->inherits<BigIntObject>(vm))
        return jsCast<JSWrapperObject*>(object)->internalValue();

    // Do not unwrap SymbolObject to Symbol. It is not performed in the spec.
    // http://www.ecma-international.org/ecma-262/6.0/#sec-serializejsonproperty
    return value;
}

static inline String gap(ExecState* exec, JSValue space)
{
    VM& vm = exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    const unsigned maxGapLength = 10;
    space = unwrapBoxedPrimitive(exec, space);
    RETURN_IF_EXCEPTION(scope, { });

    // If the space value is a number, create a gap string with that number of spaces.
    if (space.isNumber()) {
        double spaceCount = space.asNumber();
        int count;
        if (spaceCount > maxGapLength)
            count = maxGapLength;
        else if (!(spaceCount > 0))
            count = 0;
        else
            count = static_cast<int>(spaceCount);
        char spaces[maxGapLength];
        for (int i = 0; i < count; ++i)
            spaces[i] = ' ';
        return String(spaces, count);
    }

    // If the space value is a string, use it as the gap string, otherwise use no gap string.
    String spaces = space.getString(exec);
    if (spaces.length() <= maxGapLength)
        return spaces;
    return spaces.substringSharingImpl(0, maxGapLength);
}

// ------------------------------ PropertyNameForFunctionCall --------------------------------

inline PropertyNameForFunctionCall::PropertyNameForFunctionCall(const Identifier& identifier)
    : m_identifier(&identifier)
{
}

inline PropertyNameForFunctionCall::PropertyNameForFunctionCall(unsigned number)
    : m_identifier(0)
    , m_number(number)
{
}

JSValue PropertyNameForFunctionCall::value(ExecState* exec) const
{
    if (!m_value) {
        VM& vm = exec->vm();
        if (m_identifier)
            m_value = jsString(vm, m_identifier->string());
        else {
            if (m_number <= 9)
                return vm.smallStrings.singleCharacterString(m_number + '0');
            m_value = jsNontrivialString(vm, vm.numericStrings.add(m_number));
        }
    }
    return m_value;
}

// ------------------------------ Stringifier --------------------------------

Stringifier::Stringifier(ExecState* exec, JSValue replacer, JSValue space)
    : m_exec(exec)
    , m_replacer(replacer)
    , m_arrayReplacerPropertyNames(exec->vm(), PropertyNameMode::Strings, PrivateSymbolMode::Exclude)
{
    VM& vm = exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (m_replacer.isObject()) {
        JSObject* replacerObject = asObject(m_replacer);

        m_replacerCallType = CallType::None;
        if (!replacerObject->isCallable(vm, m_replacerCallType, m_replacerCallData)) {
            bool isArrayReplacer = JSC::isArray(exec, replacerObject);
            RETURN_IF_EXCEPTION(scope, );
            if (isArrayReplacer) {
                m_usingArrayReplacer = true;
                unsigned length = replacerObject->get(exec, vm.propertyNames->length).toUInt32(exec);
                RETURN_IF_EXCEPTION(scope, );
                for (unsigned i = 0; i < length; ++i) {
                    JSValue name = replacerObject->get(exec, i);
                    RETURN_IF_EXCEPTION(scope, );
                    if (name.isObject()) {
                        auto* nameObject = jsCast<JSObject*>(name);
                        if (!nameObject->inherits<NumberObject>(vm) && !nameObject->inherits<StringObject>(vm))
                            continue;
                    } else if (!name.isNumber() && !name.isString())
                        continue;
                    JSString* propertyNameString = name.toString(exec);
                    RETURN_IF_EXCEPTION(scope, );
                    auto propertyName = propertyNameString->toIdentifier(exec);
                    RETURN_IF_EXCEPTION(scope, );
                    m_arrayReplacerPropertyNames.add(WTFMove(propertyName));
                }
            }
        }
    }

    scope.release();
    m_gap = gap(exec, space);
}

JSValue Stringifier::stringify(JSValue value)
{
    VM& vm = m_exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    PropertyNameForFunctionCall emptyPropertyName(vm.propertyNames->emptyIdentifier);

    // If the replacer is not callable, root object wrapper is non-user-observable.
    // We can skip creating this wrapper object.
    JSObject* object = nullptr;
    if (isCallableReplacer()) {
        object = constructEmptyObject(m_exec);
        RETURN_IF_EXCEPTION(scope, jsUndefined());
        object->putDirect(vm, vm.propertyNames->emptyIdentifier, value);
    }

    StringBuilder result(StringBuilder::OverflowHandler::RecordOverflow);
    Holder root(Holder::RootHolder, object);
    auto stringifyResult = appendStringifiedValue(result, value, root, emptyPropertyName);
    RETURN_IF_EXCEPTION(scope, jsUndefined());
    if (UNLIKELY(result.hasOverflowed())) {
        throwOutOfMemoryError(m_exec, scope);
        return jsUndefined();
    }
    if (UNLIKELY(stringifyResult != StringifySucceeded))
        return jsUndefined();
    RELEASE_AND_RETURN(scope, jsString(vm, result.toString()));
}

ALWAYS_INLINE JSValue Stringifier::toJSON(JSValue baseValue, const PropertyNameForFunctionCall& propertyName)
{
    VM& vm = m_exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    scope.assertNoException();

    PropertySlot slot(baseValue, PropertySlot::InternalMethodType::Get);
    bool hasProperty = baseValue.getPropertySlot(m_exec, vm.propertyNames->toJSON, slot);
    EXCEPTION_ASSERT(!scope.exception() || !hasProperty);
    if (!hasProperty)
        return baseValue;

    JSValue toJSONFunction = slot.getValue(m_exec, vm.propertyNames->toJSON);
    RETURN_IF_EXCEPTION(scope, { });
    RELEASE_AND_RETURN(scope, toJSONImpl(vm, baseValue, toJSONFunction, propertyName));
}

JSValue Stringifier::toJSONImpl(VM& vm, JSValue baseValue, JSValue toJSONFunction, const PropertyNameForFunctionCall& propertyName)
{
    CallType callType;
    CallData callData;
    if (!toJSONFunction.isCallable(vm, callType, callData))
        return baseValue;

    MarkedArgumentBuffer args;
    args.append(propertyName.value(m_exec));
    ASSERT(!args.hasOverflowed());
    return call(m_exec, asObject(toJSONFunction), callType, callData, baseValue, args);
}

Stringifier::StringifyResult Stringifier::appendStringifiedValue(StringBuilder& builder, JSValue value, const Holder& holder, const PropertyNameForFunctionCall& propertyName)
{
    VM& vm = m_exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    // Call the toJSON function.
    if (value.isObject() || value.isBigInt()) {
        value = toJSON(value, propertyName);
        RETURN_IF_EXCEPTION(scope, StringifyFailed);
    }

    // Call the replacer function.
    if (isCallableReplacer()) {
        MarkedArgumentBuffer args;
        args.append(propertyName.value(m_exec));
        args.append(value);
        ASSERT(!args.hasOverflowed());
        ASSERT(holder.object());
        value = call(m_exec, m_replacer, m_replacerCallType, m_replacerCallData, holder.object(), args);
        RETURN_IF_EXCEPTION(scope, StringifyFailed);
    }

    if ((value.isUndefined() || value.isSymbol()) && !holder.isArray())
        return StringifyFailedDueToUndefinedOrSymbolValue;

    if (value.isNull()) {
        builder.appendLiteral("null");
        return StringifySucceeded;
    }

    value = unwrapBoxedPrimitive(m_exec, value);

    RETURN_IF_EXCEPTION(scope, StringifyFailed);

    if (value.isBoolean()) {
        if (value.isTrue())
            builder.appendLiteral("true");
        else
            builder.appendLiteral("false");
        return StringifySucceeded;
    }

    if (value.isString()) {
        const String& string = asString(value)->value(m_exec);
        RETURN_IF_EXCEPTION(scope, StringifyFailed);
        builder.appendQuotedJSONString(string);
        return StringifySucceeded;
    }

    if (value.isNumber()) {
        if (value.isInt32())
            builder.appendNumber(value.asInt32());
        else {
            double number = value.asNumber();
            if (!std::isfinite(number))
                builder.appendLiteral("null");
            else
                builder.appendNumber(number);
        }
        return StringifySucceeded;
    }

    if (value.isBigInt()) {
        throwTypeError(m_exec, scope, "JSON.stringify cannot serialize BigInt."_s);
        return StringifyFailed;
    }

    if (!value.isObject())
        return StringifyFailed;

    JSObject* object = asObject(value);
    if (object->isFunction(vm)) {
        if (holder.isArray()) {
            builder.appendLiteral("null");
            return StringifySucceeded;
        }
        return StringifyFailedDueToUndefinedOrSymbolValue;
    }

    if (UNLIKELY(builder.hasOverflowed()))
        return StringifyFailed;

    // Handle cycle detection, and put the holder on the stack.
    for (unsigned i = 0; i < m_holderStack.size(); i++) {
        if (m_holderStack[i].object() == object) {
            throwTypeError(m_exec, scope, "JSON.stringify cannot serialize cyclic structures."_s);
            return StringifyFailed;
        }
    }

    bool holderStackWasEmpty = m_holderStack.isEmpty();
    m_holderStack.append(Holder(m_exec, object));
    m_objectStack.appendWithCrashOnOverflow(object);
    RETURN_IF_EXCEPTION(scope, StringifyFailed);
    if (!holderStackWasEmpty)
        return StringifySucceeded;

    do {
        while (m_holderStack.last().appendNextProperty(*this, builder))
            RETURN_IF_EXCEPTION(scope, StringifyFailed);
        RETURN_IF_EXCEPTION(scope, StringifyFailed);
        if (UNLIKELY(builder.hasOverflowed()))
            return StringifyFailed;
        m_holderStack.removeLast();
        m_objectStack.removeLast();
    } while (!m_holderStack.isEmpty());
    return StringifySucceeded;
}

inline bool Stringifier::willIndent() const
{
    return !m_gap.isEmpty();
}

inline void Stringifier::indent()
{
    // Use a single shared string, m_repeatedGap, so we don't keep allocating new ones as we indent and unindent.
    unsigned newSize = m_indent.length() + m_gap.length();
    if (newSize > m_repeatedGap.length())
        m_repeatedGap = makeString(m_repeatedGap, m_gap);
    ASSERT(newSize <= m_repeatedGap.length());
    m_indent = m_repeatedGap.substringSharingImpl(0, newSize);
}

inline void Stringifier::unindent()
{
    ASSERT(m_indent.length() >= m_gap.length());
    m_indent = m_repeatedGap.substringSharingImpl(0, m_indent.length() - m_gap.length());
}

inline void Stringifier::startNewLine(StringBuilder& builder) const
{
    if (m_gap.isEmpty())
        return;
    builder.append('\n');
    builder.append(m_indent);
}

inline Stringifier::Holder::Holder(ExecState* exec, JSObject* object)
    : m_object(object)
    , m_isJSArray(isJSArray(object))
    , m_isArray(JSC::isArray(exec, object))
{
}

inline Stringifier::Holder::Holder(RootHolderTag, JSObject* object)
    : m_object(object)
    , m_isJSArray(false)
    , m_isArray(false)
{
}

bool Stringifier::Holder::appendNextProperty(Stringifier& stringifier, StringBuilder& builder)
{
    ASSERT(m_index <= m_size);

    ExecState* exec = stringifier.m_exec;
    VM& vm = exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    // First time through, initialize.
    if (!m_index) {
        if (m_isArray) {
            if (m_isJSArray)
                m_size = asArray(m_object)->length();
            else {
                JSValue value = m_object->get(exec, vm.propertyNames->length);
                RETURN_IF_EXCEPTION(scope, false);
                m_size = value.toUInt32(exec);
                RETURN_IF_EXCEPTION(scope, false);
            }
            builder.append('[');
        } else {
            if (stringifier.m_usingArrayReplacer)
                m_propertyNames = stringifier.m_arrayReplacerPropertyNames.data();
            else {
                PropertyNameArray objectPropertyNames(vm, PropertyNameMode::Strings, PrivateSymbolMode::Exclude);
                m_object->methodTable(vm)->getOwnPropertyNames(m_object, exec, objectPropertyNames, EnumerationMode());
                RETURN_IF_EXCEPTION(scope, false);
                m_propertyNames = objectPropertyNames.releaseData();
            }
            m_size = m_propertyNames->propertyNameVector().size();
            builder.append('{');
        }
        stringifier.indent();
    }
    if (UNLIKELY(builder.hasOverflowed()))
        return false;

    // Last time through, finish up and return false.
    if (m_index == m_size) {
        stringifier.unindent();
        if (m_size && builder[builder.length() - 1] != '{')
            stringifier.startNewLine(builder);
        builder.append(m_isArray ? ']' : '}');
        return false;
    }

    // Handle a single element of the array or object.
    unsigned index = m_index++;
    unsigned rollBackPoint = 0;
    StringifyResult stringifyResult;
    if (m_isArray) {
        // Get the value.
        JSValue value;
        if (m_isJSArray && asArray(m_object)->canGetIndexQuickly(index))
            value = asArray(m_object)->getIndexQuickly(index);
        else {
            PropertySlot slot(m_object, PropertySlot::InternalMethodType::Get);
            bool hasProperty = m_object->getPropertySlot(exec, index, slot);
            EXCEPTION_ASSERT(!scope.exception() || !hasProperty);
            if (hasProperty)
                value = slot.getValue(exec, index);
            else
                value = jsUndefined();
            RETURN_IF_EXCEPTION(scope, false);
        }

        // Append the separator string.
        if (index)
            builder.append(',');
        stringifier.startNewLine(builder);

        // Append the stringified value.
        stringifyResult = stringifier.appendStringifiedValue(builder, value, *this, index);
        ASSERT(stringifyResult != StringifyFailedDueToUndefinedOrSymbolValue);
    } else {
        // Get the value.
        PropertySlot slot(m_object, PropertySlot::InternalMethodType::Get);
        Identifier& propertyName = m_propertyNames->propertyNameVector()[index];
        bool hasProperty = m_object->getPropertySlot(exec, propertyName, slot);
        EXCEPTION_ASSERT(!scope.exception() || !hasProperty);
        if (!hasProperty)
            return true;
        JSValue value = slot.getValue(exec, propertyName);
        RETURN_IF_EXCEPTION(scope, false);

        rollBackPoint = builder.length();

        // Append the separator string.
        if (builder[rollBackPoint - 1] != '{')
            builder.append(',');
        stringifier.startNewLine(builder);

        // Append the property name.
        builder.appendQuotedJSONString(propertyName.string());
        builder.append(':');
        if (stringifier.willIndent())
            builder.append(' ');

        // Append the stringified value.
        stringifyResult = stringifier.appendStringifiedValue(builder, value, *this, propertyName);
    }
    RETURN_IF_EXCEPTION(scope, false);

    // From this point on, no access to the this pointer or to any members, because the
    // Holder object may have moved if the call to stringify pushed a new Holder onto
    // m_holderStack.

    switch (stringifyResult) {
        case StringifyFailed:
            builder.appendLiteral("null");
            break;
        case StringifySucceeded:
            break;
        case StringifyFailedDueToUndefinedOrSymbolValue:
            // This only occurs when get an undefined value or a symbol value for
            // an object property. In this case we don't want the separator and
            // property name that we already appended, so roll back.
            builder.resize(rollBackPoint);
            break;
    }

    return true;
}

// ------------------------------ JSONObject --------------------------------

const ClassInfo JSONObject::s_info = { "JSON", &JSNonFinalObject::s_info, &jsonTable, nullptr, CREATE_METHOD_TABLE(JSONObject) };

/* Source for JSONObject.lut.h
@begin jsonTable
  parse         JSONProtoFuncParse             DontEnum|Function 2
  stringify     JSONProtoFuncStringify         DontEnum|Function 3
@end
*/

// ECMA 15.8

class Walker {
    WTF_MAKE_NONCOPYABLE(Walker);
    WTF_FORBID_HEAP_ALLOCATION;
public:
    Walker(ExecState* exec, JSObject* function, CallType callType, CallData callData)
        : m_exec(exec)
        , m_function(function)
        , m_callType(callType)
        , m_callData(callData)
    {
    }
    JSValue walk(JSValue unfiltered);
private:
    JSValue callReviver(JSObject* thisObj, JSValue property, JSValue unfiltered)
    {
        MarkedArgumentBuffer args;
        args.append(property);
        args.append(unfiltered);
        ASSERT(!args.hasOverflowed());
        return call(m_exec, m_function, m_callType, m_callData, thisObj, args);
    }

    friend class Holder;

    ExecState* m_exec;
    JSObject* m_function;
    CallType m_callType;
    CallData m_callData;
};

// We clamp recursion well beyond anything reasonable.
static constexpr unsigned maximumFilterRecursion = 40000;
enum WalkerState { StateUnknown, ArrayStartState, ArrayStartVisitMember, ArrayEndVisitMember, 
                                 ObjectStartState, ObjectStartVisitMember, ObjectEndVisitMember };
NEVER_INLINE JSValue Walker::walk(JSValue unfiltered)
{
    VM& vm = m_exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    Vector<PropertyNameArray, 16, UnsafeVectorOverflow> propertyStack;
    Vector<uint32_t, 16, UnsafeVectorOverflow> indexStack;
    MarkedArgumentBuffer markedStack;
    Vector<unsigned, 16, UnsafeVectorOverflow> arrayLengthStack;
    
    Vector<WalkerState, 16, UnsafeVectorOverflow> stateStack;
    WalkerState state = StateUnknown;
    JSValue inValue = unfiltered;
    JSValue outValue = jsNull();
    
    while (1) {
        switch (state) {
            arrayStartState:
            case ArrayStartState: {
                ASSERT(inValue.isObject());
                ASSERT(isJSArray(inValue) || inValue.inherits<ProxyObject>(vm));
                if (markedStack.size() > maximumFilterRecursion)
                    return throwStackOverflowError(m_exec, scope);

                JSObject* array = asObject(inValue);
                markedStack.appendWithCrashOnOverflow(array);
                unsigned length = toLength(m_exec, array);
                RETURN_IF_EXCEPTION(scope, { });
                arrayLengthStack.append(length);
                indexStack.append(0);
            }
            arrayStartVisitMember:
            FALLTHROUGH;
            case ArrayStartVisitMember: {
                JSObject* array = asObject(markedStack.last());
                uint32_t index = indexStack.last();
                unsigned arrayLength = arrayLengthStack.last();
                if (index == arrayLength) {
                    outValue = array;
                    markedStack.removeLast();
                    arrayLengthStack.removeLast();
                    indexStack.removeLast();
                    break;
                }
                if (isJSArray(array) && array->canGetIndexQuickly(index))
                    inValue = array->getIndexQuickly(index);
                else {
                    PropertySlot slot(array, PropertySlot::InternalMethodType::Get);
                    if (array->methodTable(vm)->getOwnPropertySlotByIndex(array, m_exec, index, slot))
                        inValue = slot.getValue(m_exec, index);
                    else
                        inValue = jsUndefined();
                    RETURN_IF_EXCEPTION(scope, { });
                }
                    
                if (inValue.isObject()) {
                    stateStack.append(ArrayEndVisitMember);
                    goto stateUnknown;
                } else
                    outValue = inValue;
                FALLTHROUGH;
            }
            case ArrayEndVisitMember: {
                JSObject* array = asObject(markedStack.last());
                JSValue filteredValue = callReviver(array, jsString(vm, String::number(indexStack.last())), outValue);
                RETURN_IF_EXCEPTION(scope, { });
                if (filteredValue.isUndefined())
                    array->methodTable(vm)->deletePropertyByIndex(array, m_exec, indexStack.last());
                else
                    array->putDirectIndex(m_exec, indexStack.last(), filteredValue, 0, PutDirectIndexShouldNotThrow);
                RETURN_IF_EXCEPTION(scope, { });
                indexStack.last()++;
                goto arrayStartVisitMember;
            }
            objectStartState:
            case ObjectStartState: {
                ASSERT(inValue.isObject());
                ASSERT(!isJSArray(inValue));
                if (markedStack.size() > maximumFilterRecursion)
                    return throwStackOverflowError(m_exec, scope);

                JSObject* object = asObject(inValue);
                markedStack.appendWithCrashOnOverflow(object);
                indexStack.append(0);
                propertyStack.append(PropertyNameArray(vm, PropertyNameMode::Strings, PrivateSymbolMode::Exclude));
                object->methodTable(vm)->getOwnPropertyNames(object, m_exec, propertyStack.last(), EnumerationMode());
                RETURN_IF_EXCEPTION(scope, { });
            }
            objectStartVisitMember:
            FALLTHROUGH;
            case ObjectStartVisitMember: {
                JSObject* object = jsCast<JSObject*>(markedStack.last());
                uint32_t index = indexStack.last();
                PropertyNameArray& properties = propertyStack.last();
                if (index == properties.size()) {
                    outValue = object;
                    markedStack.removeLast();
                    indexStack.removeLast();
                    propertyStack.removeLast();
                    break;
                }
                PropertySlot slot(object, PropertySlot::InternalMethodType::Get);
                if (object->methodTable(vm)->getOwnPropertySlot(object, m_exec, properties[index], slot))
                    inValue = slot.getValue(m_exec, properties[index]);
                else
                    inValue = jsUndefined();

                // The holder may be modified by the reviver function so any lookup may throw
                RETURN_IF_EXCEPTION(scope, { });

                if (inValue.isObject()) {
                    stateStack.append(ObjectEndVisitMember);
                    goto stateUnknown;
                } else
                    outValue = inValue;
                FALLTHROUGH;
            }
            case ObjectEndVisitMember: {
                JSObject* object = jsCast<JSObject*>(markedStack.last());
                Identifier prop = propertyStack.last()[indexStack.last()];
                PutPropertySlot slot(object);
                JSValue filteredValue = callReviver(object, jsString(vm, prop.string()), outValue);
                RETURN_IF_EXCEPTION(scope, { });
                if (filteredValue.isUndefined())
                    object->methodTable(vm)->deleteProperty(object, m_exec, prop);
                else
                    object->methodTable(vm)->put(object, m_exec, prop, filteredValue, slot);
                RETURN_IF_EXCEPTION(scope, { });
                indexStack.last()++;
                goto objectStartVisitMember;
            }
            stateUnknown:
            case StateUnknown:
                if (!inValue.isObject()) {
                    outValue = inValue;
                    break;
                }
                bool valueIsArray = isArray(m_exec, inValue);
                RETURN_IF_EXCEPTION(scope, { });
                if (valueIsArray)
                    goto arrayStartState;
                goto objectStartState;
        }
        if (stateStack.isEmpty())
            break;

        state = stateStack.last();
        stateStack.removeLast();
    }
    JSObject* finalHolder = constructEmptyObject(m_exec);
    PutPropertySlot slot(finalHolder);
    finalHolder->methodTable(vm)->put(finalHolder, m_exec, vm.propertyNames->emptyIdentifier, outValue, slot);
    RETURN_IF_EXCEPTION(scope, { });
    RELEASE_AND_RETURN(scope, callReviver(finalHolder, jsEmptyString(vm), outValue));
}

// ECMA-262 v5 15.12.2
EncodedJSValue JSC_HOST_CALL JSONProtoFuncParse(JSGlobalObject* globalObject, CallFrame* callFrame)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    auto viewWithString = callFrame->argument(0).toString(callFrame)->viewWithUnderlyingString(callFrame);
    RETURN_IF_EXCEPTION(scope, { });
    StringView view = viewWithString.view;

    JSValue unfiltered;
    if (view.is8Bit()) {
        LiteralParser<LChar> jsonParser(callFrame, view.characters8(), view.length(), StrictJSON);
        unfiltered = jsonParser.tryLiteralParse();
        EXCEPTION_ASSERT(!scope.exception() || !unfiltered);
        if (!unfiltered) {
            RETURN_IF_EXCEPTION(scope, { });
            return throwVMError(callFrame, scope, createSyntaxError(callFrame, jsonParser.getErrorMessage()));
        }
    } else {
        LiteralParser<UChar> jsonParser(callFrame, view.characters16(), view.length(), StrictJSON);
        unfiltered = jsonParser.tryLiteralParse();
        EXCEPTION_ASSERT(!scope.exception() || !unfiltered);
        if (!unfiltered) {
            RETURN_IF_EXCEPTION(scope, { });
            return throwVMError(callFrame, scope, createSyntaxError(callFrame, jsonParser.getErrorMessage()));
        }
    }
    
    if (callFrame->argumentCount() < 2)
        return JSValue::encode(unfiltered);
    
    JSValue function = callFrame->uncheckedArgument(1);
    CallData callData;
    CallType callType = getCallData(vm, function, callData);
    if (callType == CallType::None)
        return JSValue::encode(unfiltered);
    scope.release();
    Walker walker(callFrame, asObject(function), callType, callData);
    return JSValue::encode(walker.walk(unfiltered));
}

// ECMA-262 v5 15.12.3
EncodedJSValue JSC_HOST_CALL JSONProtoFuncStringify(JSGlobalObject* globalObject, CallFrame* callFrame)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    Stringifier stringifier(callFrame, callFrame->argument(1), callFrame->argument(2));
    RETURN_IF_EXCEPTION(scope, { });
    RELEASE_AND_RETURN(scope, JSValue::encode(stringifier.stringify(callFrame->argument(0))));
}

JSValue JSONParse(ExecState* exec, const String& json)
{
    if (json.isNull())
        return JSValue();

    if (json.is8Bit()) {
        LiteralParser<LChar> jsonParser(exec, json.characters8(), json.length(), StrictJSON);
        return jsonParser.tryLiteralParse();
    }

    LiteralParser<UChar> jsonParser(exec, json.characters16(), json.length(), StrictJSON);
    return jsonParser.tryLiteralParse();
}

String JSONStringify(ExecState* exec, JSValue value, JSValue space)
{
    VM& vm = exec->vm();
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    Stringifier stringifier(exec, jsNull(), space);
    RETURN_IF_EXCEPTION(throwScope, { });
    JSValue result = stringifier.stringify(value);
    if (UNLIKELY(throwScope.exception()) || result.isUndefinedOrNull())
        return String();
    return result.getString(exec);
}

String JSONStringify(ExecState* exec, JSValue value, unsigned indent)
{
    return JSONStringify(exec, value, jsNumber(indent));
}

} // namespace JSC
