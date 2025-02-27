/*
 *  Copyright (C) 1999-2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2003, 2008, 2016 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include "BooleanConstructor.h"

#include "BooleanPrototype.h"
#include "JSGlobalObject.h"
#include "JSCInlines.h"

namespace JSC {

STATIC_ASSERT_IS_TRIVIALLY_DESTRUCTIBLE(BooleanConstructor);

const ClassInfo BooleanConstructor::s_info = { "Function", &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(BooleanConstructor) };

// ECMA 15.6.1
static EncodedJSValue JSC_HOST_CALL callBooleanConstructor(JSGlobalObject*, CallFrame* callFrame)
{
    return JSValue::encode(jsBoolean(callFrame->argument(0).toBoolean(callFrame)));
}

// ECMA 15.6.2
static EncodedJSValue JSC_HOST_CALL constructWithBooleanConstructor(JSGlobalObject* globalObject, CallFrame* callFrame)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSValue boolean = jsBoolean(callFrame->argument(0).toBoolean(callFrame));
    Structure* booleanStructure = InternalFunction::createSubclassStructure(callFrame, callFrame->newTarget(), globalObject->booleanObjectStructure());
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    BooleanObject* obj = BooleanObject::create(vm, booleanStructure);
    obj->setInternalValue(vm, boolean);
    return JSValue::encode(obj);
}

BooleanConstructor::BooleanConstructor(VM& vm, Structure* structure)
    : InternalFunction(vm, structure, callBooleanConstructor, constructWithBooleanConstructor)
{
}

void BooleanConstructor::finishCreation(VM& vm, BooleanPrototype* booleanPrototype)
{
    Base::finishCreation(vm, vm.propertyNames->Boolean.string(), NameVisibility::Visible, NameAdditionMode::WithoutStructureTransition);
    putDirectWithoutTransition(vm, vm.propertyNames->prototype, booleanPrototype, PropertyAttribute::DontEnum | PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly);
    putDirectWithoutTransition(vm, vm.propertyNames->length, jsNumber(1), PropertyAttribute::ReadOnly | PropertyAttribute::DontEnum);
}

JSObject* constructBooleanFromImmediateBoolean(ExecState*, JSGlobalObject* globalObject, JSValue immediateBooleanValue)
{
    VM& vm = globalObject->vm();
    BooleanObject* obj = BooleanObject::create(vm, globalObject->booleanObjectStructure());
    obj->setInternalValue(vm, immediateBooleanValue);
    return obj;
}

} // namespace JSC
