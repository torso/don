#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "heap.h"
#include "math.h"
#include "work.h"
#include "vm.h"

static const char *getString(vref object)
{
    const SubString *ss;
    VType type;

start:
    assert(!HeapIsFutureValue(object));

    type = HeapGetObjectType(object);
    switch ((int)type)
    {
    case TYPE_STRING:
        return (const char*)HeapGetObjectData(object);

    case TYPE_STRING_WRAPPED:
        return *(const char**)HeapGetObjectData(object);

    case TYPE_SUBSTRING:
        ss = (const SubString*)HeapGetObjectData(object);
        return &getString(ss->string)[ss->offset];

    case TYPE_VALUE:
        object = *(vref*)HeapGetObjectData(object);
        goto start;
    }
    unreachable;
}

bool VWait(vref *value)
{
    HeapObject ho;

    for (;;)
    {
        HeapGet(*value, &ho);
        if (ho.type & TYPE_FLAG_FUTURE)
        {
            return false;
        }
        if (ho.type != TYPE_VALUE)
        {
            return true;
        }
        *value = *(vref*)ho.data;
    }
}

VBool VGetBool(vref value)
{
    HeapObject ho;

start:
    HeapGet(value, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return FUTURE;
    }
    switch ((int)ho.type)
    {
    case TYPE_BOOLEAN_TRUE:
    case TYPE_FILE:
        return TRUTHY;
    case TYPE_BOOLEAN_FALSE:
    case TYPE_NULL:
        return FALSY;

    case TYPE_INTEGER:
        return HeapUnboxInteger(value) ? TRUTHY : FALSY;

    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        return VStringLength(value) ? TRUTHY : FALSY;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return VCollectionSize(value) ? TRUTHY : FALSY;

    case TYPE_VALUE:
        value = *(vref*)ho.data;
        goto start;
    }
    unreachable;
}

bool VIsTruthy(vref value)
{
    return VGetBool(value) == TRUTHY;
}

bool VIsFalsy(vref value)
{
    return VGetBool(value) == FALSY;
}


static vref delay(VM *vm, WorkFunction function, VType type, size_t valueCount, const vref *values)
{
    byte *objectData = HeapAlloc(type, valueCount * sizeof(vref));
    assert(valueCount);
    memcpy(objectData, values, valueCount * sizeof(vref));
    {
        vref result = HeapFinishAlloc(objectData);
        vref *p;
        Work *work = WorkAdd(function, vm, 1, &p);
        if (DEBUG_FUTURE)
        {
            char *strResult = HeapDebug(result);
            printf("future %s = %ld(...)\n", strResult, valueCount);
            free(strResult);
        }
        *p = result;
        WorkCommit(work);
        return result;
    }
}

static vref delayBinary(VM *vm, WorkFunction function, VType type, vref value1, vref value2)
{
    byte *objectData = HeapAlloc(type, 2 * sizeof(vref));
    vref *values = (vref*)objectData;
    values[0] = value1;
    values[1] = value2;
    {
        vref result = HeapFinishAlloc(objectData);
        vref *p;
        Work *work = WorkAdd(function, vm, 1, &p);
        if (DEBUG_FUTURE)
        {
            char *strResult = HeapDebug(result);
            char *strValue1 = HeapDebug(value1);
            char *strValue2 = HeapDebug(value2);
            printf("future %s = %s %s\n", strResult, strValue1, strValue2);
            free(strResult);
            free(strValue1);
            free(strValue2);
        }
        *p = result;
        WorkCommit(work);
        return result;
    }
}

static vref delayUnary(VM *vm, WorkFunction function, VType type, vref value)
{
    byte *objectData = HeapAlloc(type, sizeof(vref));
    *(vref*)objectData = value;
    {
        vref result = HeapFinishAlloc(objectData);
        vref *p;
        Work *work = WorkAdd(function, vm, 1, &p);
        if (DEBUG_FUTURE)
        {
            char *strResult = HeapDebug(result);
            char *strValue = HeapDebug(value);
            printf("future %s = %s\n", strResult, strValue);
            free(strResult);
            free(strValue);
        }
        *p = result;
        WorkCommit(work);
        return result;
    }
}

static vref doEquals(vref value1, vref value2)
{
    HeapObject ho1;
    HeapObject ho2;

    if (value1 == value2)
    {
        return HeapTrue;
    }
    HeapGet(value1, &ho1);
    HeapGet(value2, &ho2);
    while (ho1.type == TYPE_VALUE)
    {
        value1 = *(vref*)ho1.data;
        if (value1 == value2)
        {
            return HeapTrue;
        }
        HeapGet(value1, &ho1);
    }
    if (ho2.type == TYPE_VALUE)
    {
        do
        {
            value2 = *(vref*)ho2.data;
            HeapGet(value2, &ho2);
        }
        while (ho2.type == TYPE_VALUE);
        if (value1 == value2)
        {
            return HeapTrue;
        }
    }

    if ((ho1.type | ho2.type) & TYPE_FLAG_FUTURE)
    {
        return 0;
    }

    switch ((int)ho1.type)
    {
    case TYPE_NULL:
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
        return HeapFalse;

    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        if (!VIsStringType(ho2.type))
        {
            return HeapFalse;
        }
        {
            size_t size1 = VStringLength(value1);
            size_t size2 = VStringLength(value2);
            return size1 == size2 &&
                !memcmp(getString(value1), getString(value2), size1) ? HeapTrue : HeapFalse;
        }

    case TYPE_FILE:
        return HeapFalse;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        if (!VIsCollectionType(ho2.type))
        {
            return HeapFalse;
        }
        {
            size_t size1 = VCollectionSize(value1);
            size_t size2 = VCollectionSize(value2);
            size_t index;
            if (size1 != size2)
            {
                return HeapFalse;
            }
            for (index = 0; index < size1; index++)
            {
                vref item1;
                vref item2;
                vref result;
                bool success =
                    VCollectionGet(value1, HeapBoxSize(index), &item1) &&
                    VCollectionGet(value2, HeapBoxSize(index), &item2);
                assert(success);
                result = doEquals(item1, item2);
                if (result != HeapTrue)
                {
                    return result;
                }
            }
        }
        return HeapTrue;

    default:
        unreachable;
    }
}

static bool workEquals(Work *work unused, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doEquals(operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VEquals(VM *vm, vref value1, vref value2)
{
    vref value = doEquals(value1, value2);
    if (!value)
    {
        return delayBinary(vm, workEquals, TYPE_FUTURE_EQUALS, value1, value2);
    }
    return value;
}

static bool workNotEquals(Work *work unused, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doEquals(operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result == HeapTrue ? HeapFalse : HeapTrue);
        return true;
    }
    return false;
}

vref VNotEquals(VM *vm, vref value1, vref value2)
{
    vref value = doEquals(value1, value2);
    if (!value)
    {
        return delayBinary(vm, workNotEquals, TYPE_FUTURE_NOT_EQUALS, value1, value2);
    }
    return value == HeapTrue ? HeapFalse : HeapTrue;
}

static vref doLess(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    case TYPE_FUTURE:
        return 0;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    case TYPE_FUTURE:
        return 0;
    default:
        goto error;
    }
    return HeapUnboxInteger(value1) < HeapUnboxInteger(value2) ?
        HeapTrue : HeapFalse;

error:
    VMFail(vm, ip, "Cannot compare non-numbers");
    return HeapNull;
}

static bool workLess(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doLess(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VLess(VM *vm, vref value1, vref value2)
{
    vref value = doLess(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workLess, TYPE_FUTURE_LESS, value1, value2);
    }
    return value;
}

static vref doLessEquals(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    case TYPE_FUTURE:
        return 0;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    case TYPE_FUTURE:
        return 0;
    default:
        goto error;
    }
    return HeapUnboxInteger(value1) <= HeapUnboxInteger(value2) ?
        HeapTrue : HeapFalse;

error:
    VMFail(vm, ip, "Cannot compare non-numbers");
    return HeapNull;
}

static bool workLessEquals(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doLessEquals(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VLessEquals(VM *vm, vref value1, vref value2)
{
    vref value = doLessEquals(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workLessEquals, TYPE_FUTURE_LESS_EQUALS, value1, value2);
    }
    return value;
}

static vref doAdd(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        /* TODO: Check if value2 is zero */
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    case TYPE_FUTURE:
        return 0;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return HeapUnboxInteger(value1) ? 0 : value2;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    default:
        goto error;
    }
    return HeapBoxInteger(HeapUnboxInteger(value1) + HeapUnboxInteger(value2));

error:
    VMFail(vm, ip, "Cannot add non-numbers. Use \"$a$b\" to concatenate strings");
    return HeapNull;
}

static bool workAdd(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doAdd(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VAdd(VM *vm, vref value1, vref value2)
{
    vref value = doAdd(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workAdd, TYPE_FUTURE_ADD, value1, value2);
    }
    return value;
}

static vref doSub(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    case TYPE_FUTURE:
        return 0;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    case TYPE_FUTURE:
        return 0;
    default:
        goto error;
    }
    return HeapBoxInteger(HeapUnboxInteger(value1) - HeapUnboxInteger(value2));

error:
    VMFail(vm, ip, "Cannot subtract non-numbers");
    return HeapNull;
}

static bool workSub(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doSub(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VSub(VM *vm, vref value1, vref value2)
{
    vref value = doSub(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workSub, TYPE_FUTURE_SUB, value1, value2);
    }
    return value;
}

static vref doMul(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    default:
        goto error;
    }
    return HeapBoxInteger(HeapUnboxInteger(value1) * HeapUnboxInteger(value2));

error:
    VMFail(vm, ip, "Cannot multiply non-numbers");
    return HeapNull;
}

static bool workMul(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doMul(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VMul(VM *vm, vref value1, vref value2)
{
    vref value = doMul(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workMul, TYPE_FUTURE_MUL, value1, value2);
    }
    return value;
}

static vref doDiv(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    default:
        goto error;
    }
    assert((HeapUnboxInteger(value1) / HeapUnboxInteger(value2)) * HeapUnboxInteger(value2) ==
           HeapUnboxInteger(value1)); /* TODO: fraction */
    return HeapBoxInteger(HeapUnboxInteger(value1) / HeapUnboxInteger(value2));

error:
    VMFail(vm, ip, "Cannot divide non-numbers");
    return HeapNull;
}

static bool workDiv(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doDiv(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VDiv(VM *vm, vref value1, vref value2)
{
    vref value = doDiv(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workDiv, TYPE_FUTURE_DIV, value1, value2);
    }
    return value;
}

static vref doRem(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    default:
        goto error;
    }
    return HeapBoxInteger(HeapUnboxInteger(value1) % HeapUnboxInteger(value2));

error:
    VMFail(vm, ip, "Cannot divide non-numbers");
    return HeapNull;
}

static bool workRem(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doRem(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VRem(VM *vm, vref value1, vref value2)
{
    vref value = doRem(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workRem, TYPE_FUTURE_REM, value1, value2);
    }
    return value;
}

static vref doAnd(vref value1, vref value2)
{
    VBool b1 = VGetBool(value1);
    VBool b2 = VGetBool(value2);

    if (b1 == FALSY || b2 == FALSY)
    {
        return HeapFalse;
    }
    if (b1 == TRUTHY && b2 == TRUTHY)
    {
        return HeapTrue;
    }

    assert(b1 == FUTURE || b2 == FUTURE);
    return 0;
}

static bool workAnd(Work *work unused, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doAnd(operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VAnd(VM *vm, vref value1, vref value2)
{
    vref value = doAnd(value1, value2);
    if (!value)
    {
        return delayBinary(vm, workAnd, TYPE_FUTURE_AND, value1, value2);
    }
    return value;
}

static vref doNot(vref value)
{
    switch (VGetBool(value))
    {
    case TRUTHY:
        return HeapFalse;
    case FALSY:
        return HeapTrue;
    case FUTURE:
        return 0;
    }
    unreachable;
}

static bool workNot(Work *work unused, vref *values)
{
    vref value = *values;
    const vref *operand = (const vref*)HeapGetObjectData(value);
    vref result = doNot(*operand);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VNot(VM *vm, vref value)
{
    vref result = doNot(value);
    if (!result)
    {
        return delayUnary(vm, workNot, TYPE_FUTURE_NOT, value);
    }
    return result;
}

static vref doNeg(VM *vm, const int *ip, vref value)
{
    HeapObject ho;

checkType:
    HeapGet(value, &ho);
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        return HeapBoxInteger(-HeapUnboxInteger(value));
    case TYPE_VALUE:
        value = *(vref*)ho.data;
        goto checkType;
    case TYPE_FUTURE:
        return 0;
    }

    VMFail(vm, ip, "Cannot negate non-number.");
    return HeapNull;
}

static bool workNeg(Work *work, vref *values)
{
    vref value = *values;
    const vref *operand = (const vref*)HeapGetObjectData(value);
    vref result = doNeg(work->vm, work->ip, *operand);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VNeg(VM *vm, vref value)
{
    vref result = doNeg(vm, vm->ip, value);
    if (!result)
    {
        return delayUnary(vm, workNeg, TYPE_FUTURE_NEG, value);
    }
    return result;
}

static vref doInv(VM *vm, const int *ip, vref value)
{
    HeapObject ho;

checkType:
    HeapGet(value, &ho);
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        return HeapBoxInteger(~HeapUnboxInteger(value));
    case TYPE_VALUE:
        value = *(vref*)ho.data;
        goto checkType;
    case TYPE_FUTURE:
        return 0;
    }

    VMFail(vm, ip, "Cannot invert non-number.");
    return HeapNull;
}

static bool workInv(Work *work, vref *values)
{
    vref value = *values;
    const vref *operand = (const vref*)HeapGetObjectData(value);
    vref result = doInv(work->vm, work->ip, *operand);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VInv(VM *vm, vref value)
{
    vref result = doInv(vm, vm->ip, value);
    if (!result)
    {
        return delayUnary(vm, workInv, TYPE_FUTURE_INV, value);
    }
    return result;
}

static vref doValidIndex(VM *vm, const int *ip, vref collection, vref index)
{
    HeapObject hoIndex;
    HeapObject ho;

    for (;;)
    {
        HeapGet(index, &hoIndex);
        if (hoIndex.type != TYPE_VALUE)
        {
            break;
        }
        index = *(vref*)hoIndex.data;
    }
    if (hoIndex.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    assert(hoIndex.type == TYPE_INTEGER);

checkCollection:
    HeapGet(collection, &ho);
    switch ((int)ho.type)
    {
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return HeapUnboxSize(index) < VCollectionSize(collection) ? HeapTrue : HeapFalse;

    case TYPE_VALUE:
        collection = *(vref*)ho.data;
        goto checkCollection;

    default:
        if (ho.type & TYPE_FLAG_FUTURE)
        {
            return 0;
        }
        VMFail(vm, ip, "Can't iterate over non-collection type");
        return HeapNull;
    }
    unreachable;
}

static bool workValidIndex(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doValidIndex(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VValidIndex(VM *vm, vref collection, vref index)
{
    vref value = doValidIndex(vm, vm->ip, collection, index);
    if (!value)
    {
        return delayBinary(vm, workValidIndex, TYPE_FUTURE_VALID_INDEX, collection, index);
    }
    return value;
}

static vref doIndexedAccess(VM *vm, const int *ip, vref value1, vref value2)
{
    vref value;
    HeapObject hoIndex;
    HeapObject ho;

checkType2:
    HeapGet(value2, &hoIndex);
    if (hoIndex.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)hoIndex.type)
    {
    case TYPE_INTEGER:
    case TYPE_INTEGER_RANGE:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)hoIndex.data;
        goto checkType2;
    default:
        VMFail(vm, ip, "Index must be an integer");
        return HeapNull;
    }
checkType1:
    HeapGet(value1, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        if (hoIndex.type != TYPE_INTEGER)
        {
            VMFail(vm, ip, "Index must be an integer"); /* TODO: Range */
            return HeapNull;
        }
        if (!VCollectionGet(value1, value2, &value))
        {
            VMFail(vm, ip, "Array index out of bounds");
            return HeapNull;
        }
        return value;

    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        if (hoIndex.type == TYPE_INTEGER_RANGE)
        {
            size_t size1 = HeapUnboxSize(HeapRangeLow(value2));
            size_t size2 = HeapUnboxSize(HeapRangeHigh(value2));
            assert(size2 >= size1); /* TODO: Support inverted ranges. */
            return HeapCreateSubstring(value1, size1, size2 - size1 + 1);
        }
        else
        {
            assert(hoIndex.type == TYPE_INTEGER);
            return HeapCreateSubstring(value1, HeapUnboxSize(value2), 1);
        }

    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;

    default:
        VMFail(vm, ip, "Can't do indexed access on non-collection and non-string type");
        return HeapNull;
    }
    unreachable;
}

static bool workIndexedAccess(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doIndexedAccess(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VIndexedAccess(VM *vm, vref value1, vref value2)
{
    vref value = doIndexedAccess(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workIndexedAccess, TYPE_FUTURE_INDEXED_ACCESS, value1, value2);
    }
    return value;
}

static vref doRange(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_INTEGER:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    default:
        goto error;
    }
    return HeapCreateRange(value1, value2);

error:
    VMFail(vm, ip, "Range operands must be numbers");
    return HeapNull;
}

static bool workRange(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doRange(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VRange(VM *vm, vref value1, vref value2)
{
    vref value = doRange(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workRange, TYPE_FUTURE_RANGE, value1, value2);
    }
    return value;
}

static vref doConcat(VM *vm, const int *ip, vref value1, vref value2)
{
    HeapObject ho;

checkType1:
    HeapGet(value1, &ho);
    switch ((int)ho.type)
    {
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        break;
    case TYPE_VALUE:
        value1 = *(vref*)ho.data;
        goto checkType1;
    default:
        goto error;
    }
checkType2:
    HeapGet(value2, &ho);
    if (ho.type & TYPE_FLAG_FUTURE)
    {
        return 0;
    }
    switch ((int)ho.type)
    {
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        break;
    case TYPE_VALUE:
        value2 = *(vref*)ho.data;
        goto checkType2;
    default:
        goto error;
    }
    return VConcatList(value1, value2);

error:
    VMFail(vm, ip, "Concat operands must be lists");
    return HeapNull;
}

static bool workConcat(Work *work, vref *values)
{
    vref value = *values;
    const vref *operands = (const vref*)HeapGetObjectData(value);
    vref result = doConcat(work->vm, work->ip, operands[0], operands[1]);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VConcat(VM *vm, vref value1, vref value2)
{
    vref value = doConcat(vm, vm->ip, value1, value2);
    if (!value)
    {
        return delayBinary(vm, workConcat, TYPE_FUTURE_CONCAT, value1, value2);
    }
    return value;
}

static vref doConcatString(size_t valueCount, vref *values)
{
    size_t i;
    size_t length = 0;
    vref string;
    char *data;

    for (i = 0; i < valueCount; i++)
    {
        HeapObject ho;
        vref value = values[i];
        goto l1;
        while (ho.type == TYPE_VALUE)
        {
            value = *(vref*)ho.data;
            values[i] = value;
    l1:
            HeapGet(value, &ho);
            if (ho.type & TYPE_FLAG_FUTURE)
            {
                return 0;
            }
        }
        length += VStringLength(value);
    }
    if (!length)
    {
        return HeapEmptyString;
    }
    string = HeapCreateUninitialisedString(length, &data);
    for (i = 0; i < valueCount; i++)
    {
        data = VWriteString(values[i], data);
    }
    return string;
}

static bool workConcatString(Work *work unused, vref *values)
{
    HeapObject ho;
    vref value = *values;
    vref result;
    HeapGet(value, &ho);
    result = doConcatString(ho.size / sizeof(vref), (vref*)ho.data);
    if (result)
    {
        HeapSetFutureValue(value, result);
        return true;
    }
    return false;
}

vref VConcatString(VM *vm, size_t count, vref *values)
{
    vref value = doConcatString(count, values);
    if (!value)
    {
        return delay(vm, workConcatString, TYPE_FUTURE_CONCAT_STRING, count, values);
    }
    return value;
}


bool VIsStringType(VType type)
{
    switch ((int)type)
    {
    case TYPE_NULL:
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return false;

    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        return true;
    }
    unreachable;
}

size_t VStringLength(vref value)
{
    uint i;
    size_t size;
    size_t index;
    vref item;
    HeapObject ho;

start:
    assert(!HeapIsFutureValue(value));
    HeapGet(value, &ho);
    switch ((int)ho.type)
    {
    case TYPE_NULL:
        return 4;

    case TYPE_BOOLEAN_TRUE:
        return 4;

    case TYPE_BOOLEAN_FALSE:
        return 5;

    case TYPE_INTEGER:
        i = (uint)HeapUnboxInteger(value);
        size = 1;
        if ((int)i < 0)
        {
            size = 2;
            i = -i;
        }
        while (i > 9)
        {
            i /= 10;
            size++;
        }
        return size;

    case TYPE_STRING:
        return ho.size - 1;

    case TYPE_STRING_WRAPPED:
        return *(size_t*)&ho.data[sizeof(const char**)];

    case TYPE_SUBSTRING:
        return ((const SubString*)ho.data)->length;

    case TYPE_FILE:
        return VStringLength(*(ref_t*)ho.data);

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        size = VCollectionSize(value);
        if (size)
        {
            size--;
        }
        size = size + 6;
        for (index = 0; VCollectionGet(value, HeapBoxSize(index++), &item);)
        {
            size += VStringLength(item);
        }
        return size;

    case TYPE_VALUE:
        value = *(vref*)ho.data;
        goto start;
    }
    unreachable;
}

char *VWriteString(vref value, char *dst)
{
    size_t size;
    uint i;
    size_t index;
    vref item;
    HeapObject ho;
    const SubString *subString;

start:
    assert(!HeapIsFutureValue(value));
    HeapGet(value, &ho);
    switch ((int)ho.type)
    {
    case TYPE_NULL:
        *dst++ = 'n';
        *dst++ = 'u';
        *dst++ = 'l';
        *dst++ = 'l';
        return dst;

    case TYPE_BOOLEAN_TRUE:
        *dst++ = 't';
        *dst++ = 'r';
        *dst++ = 'u';
        *dst++ = 'e';
        return dst;

    case TYPE_BOOLEAN_FALSE:
        *dst++ = 'f';
        *dst++ = 'a';
        *dst++ = 'l';
        *dst++ = 's';
        *dst++ = 'e';
        return dst;

    case TYPE_INTEGER:
        i = (uint)HeapUnboxInteger(value);
        if (!i)
        {
            *dst++ = '0';
            return dst;
        }
        size = VStringLength(value);
        if ((int)i < 0)
        {
            *dst++ = '-';
            size--;
            i = -i;
        }
        dst += size - 1;
        while (i)
        {
            *dst-- = (char)('0' + i % 10);
            i /= 10;
        }
        return dst + size + 1;

    case TYPE_STRING:
        memcpy(dst, ho.data, ho.size - 1);
        return dst + ho.size - 1;

    case TYPE_STRING_WRAPPED:
        size = *(size_t*)&ho.data[sizeof(const char**)];
        memcpy(dst, *(const char**)ho.data, size);
        return dst + size;

    case TYPE_SUBSTRING:
        subString = (const SubString*)ho.data;
        return HeapWriteSubstring(subString->string, subString->offset, subString->length, dst);

    case TYPE_FILE:
        return VWriteString(*(vref*)ho.data, dst);

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        *dst++ = 'l';
        *dst++ = 'i';
        *dst++ = 's';
        *dst++ = 't';
        *dst++ = '(';
        for (index = 0; VCollectionGet(value, HeapBoxSize(index), &item);
             index++)
        {
            if (index)
            {
                *dst++ = ',';
            }
            dst = VWriteString(item, dst);
        }
        *dst++ = ')';
        return dst;

    case TYPE_VALUE:
        value = *(vref*)ho.data;
        goto start;
    }
    unreachable;
}



vref *VCreateArray(size_t size)
{
    return (vref*)HeapAlloc(TYPE_ARRAY, size * sizeof(vref));
}

vref VFinishArray(vref *array)
{
    return HeapFinishAlloc((byte*)array);
}

vref VCreateArrayFromData(const vref *values, size_t size)
{
    byte *data;
    size *= sizeof(vref);
    data = HeapAlloc(TYPE_ARRAY, size);
    memcpy(data, values, size);
    return HeapFinishAlloc(data);
}

vref VCreateArrayFromVector(const intvector *values)
{
    return VCreateArrayFromVectorSegment(values, 0, IVSize(values));
}

vref VCreateArrayFromVectorSegment(const intvector *values,
                                      size_t start, size_t length)
{
    if (!length)
    {
        return HeapEmptyList;
    }
    return VCreateArrayFromData((const vref*)IVGetPointer(values, start), length);
}

vref VConcatList(vref list1, vref list2)
{
    byte *data;
    vref *subLists;

    assert(VIsCollection(list1));
    assert(VIsCollection(list2));
    if (!VCollectionSize(list1))
    {
        return list2;
    }
    if (!VCollectionSize(list2))
    {
        return list1;
    }
    data = HeapAlloc(TYPE_CONCAT_LIST, sizeof(vref) * 2);
    subLists = (vref*)data;
    subLists[0] = list1;
    subLists[1] = list2;
    return HeapFinishAlloc(data);
}

bool VIsCollectionType(VType type)
{
    switch ((int)type)
    {
    case TYPE_NULL:
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
    case TYPE_FILE:
        return false;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return true;
    }
    unreachable;
}

bool VIsCollection(vref object)
{
    return VIsCollectionType(HeapGetObjectType(object));
}

size_t VCollectionSize(vref value)
{
    const byte *data;
    const int *intData;
    const vref *values;
    const vref *limit;
    size_t size;
    HeapObject ho;

start:
    assert(!HeapIsFutureValue(value));
    HeapGet(value, &ho);
    switch ((int)ho.type)
    {
    case TYPE_ARRAY:
        return ho.size / sizeof(vref);

    case TYPE_INTEGER_RANGE:
        intData = (const int*)ho.data;
        assert(!subOverflow(intData[1], intData[0]));
        return (size_t)(intData[1] - intData[0]) + 1;

    case TYPE_CONCAT_LIST:
        data = ho.data;
        values = (const vref*)data;
        limit = (const vref*)(data + ho.size);
        size = 0;
        while (values < limit)
        {
            size += VCollectionSize(*values++);
        }
        return size;

    case TYPE_VALUE:
        value = *(vref*)ho.data;
        goto start;
    }
    unreachable;
}

bool VCollectionGet(vref object, vref indexObject, vref *restrict value)
{
    const vref *restrict limit;
    const int *restrict intData;
    ssize_t i;
    size_t index;
    size_t size;
    VType type;

    assert(!HeapIsFutureValue(object));
    assert(!HeapIsFutureValue(indexObject));

    i = HeapUnboxInteger(indexObject);
    if (i < 0)
    {
        return false;
    }
    index = (size_t)i;
    if (index >= VCollectionSize(object))
    {
        return false;
    }
start:
    type = HeapGetObjectType(object);
    switch ((int)type)
    {
    case TYPE_ARRAY:
    {
        vref *restrict data = (vref*)HeapGetObjectData(object);
        *value = data[index];
        VWait(value);
        return true;
    }

    case TYPE_INTEGER_RANGE:
        intData = (const int *)HeapGetObjectData(object);
        assert(i <= INT_MAX - 1);
        assert(!addOverflow((int)i, intData[0]));
        *value = HeapBoxInteger((int)i + intData[0]);
        return true;

    case TYPE_CONCAT_LIST:
    {
        const vref *restrict data = (const vref*)HeapGetObjectData(object);
        limit = data + HeapGetObjectSize(object);
        while (data < limit)
        {
            size = VCollectionSize(*data);
            if (index < size)
            {
                assert(index <= INT_MAX);
                return VCollectionGet(*data, HeapBoxSize(index), value);
            }
            index -= size;
            data++;
        }
        return false;
    }

    case TYPE_VALUE:
        object = *(vref*)HeapGetObjectData(object);
        goto start;
    }
    unreachable;
}
