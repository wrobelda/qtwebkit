// Copyright 2019 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Compare two values structurally
defines: [assert.deepEqual]
---*/

// @ts-check

var deepEqual = (function () {
    /**
     * @typedef {0} UNKNOWN
     * @typedef {1} EQUAL
     * @typedef {-1} NOT_EQUAL
     * @typedef {Map<unknown, Map<unknown, EQUAL | NOT_EQUAL>>} ComparisonCache
     */

    /** @type {EQUAL} */
    var EQUAL = 1;

    /** @type {NOT_EQUAL} */
    var NOT_EQUAL = -1;

    /** @type {UNKNOWN} */
    var UNKNOWN = 0;

    /**
     * @template T
     * @param {T} a
     * @param {T} b
     * @returns {boolean}
     */
    function deepEqual(a, b) {
        return compareEquality(a, b) === EQUAL;
    }

    /**
     * @param {unknown} a
     * @param {unknown} b
     * @param {ComparisonCache} [cache]
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareEquality(a, b, cache) {
        return compareIf(a, b, isOptional, compareOptionality)
            || compareIf(a, b, isPrimitiveEquatable, comparePrimitiveEquality)
            || compareIf(a, b, isObjectEquatable, compareObjectEquality, cache)
            || NOT_EQUAL;
    }

    /**
     * @template T
     * @param {unknown} a
     * @param {unknown} b
     * @param {(value: unknown) => value is T} test
     * @param {(a: T, b: T, cache?: ComparisonCache) => EQUAL | NOT_EQUAL} compare
     * @param {ComparisonCache} [cache]
     * @returns {EQUAL | NOT_EQUAL | UNKNOWN}
     */
    function compareIf(a, b, test, compare, cache) {
        return !test(a)
            ? !test(b) ? UNKNOWN : NOT_EQUAL
            : !test(b) ? NOT_EQUAL : cacheComparison(a, b, compare, cache);
    }

    /**
     * @returns {EQUAL | UNKNOWN}
     */
    function tryCompareStrictEquality(a, b) {
        return a === b ? EQUAL : UNKNOWN;
    }

    /**
     * @returns {NOT_EQUAL | UNKNOWN}
     */
    function tryCompareTypeOfEquality(a, b) {
        return typeof a !== typeof b ? NOT_EQUAL : UNKNOWN;
    }

    /**
     * @returns {NOT_EQUAL | UNKNOWN}
     */
    function tryCompareToStringTagEquality(a, b) {
        var aTag = Symbol.toStringTag in a ? a[Symbol.toStringTag] : undefined;
        var bTag = Symbol.toStringTag in b ? b[Symbol.toStringTag] : undefined;
        return aTag !== bTag ? NOT_EQUAL : UNKNOWN;
    }

    /**
     * @returns {value is null | undefined}
     */
    function isOptional(value) {
        return value === undefined
            || value === null;
    }

    /**
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareOptionality(a, b) {
        return tryCompareStrictEquality(a, b)
            || NOT_EQUAL;
    }

    /**
     * @returns {value is number | bigint | string | symbol | boolean | undefined}
     */
    function isPrimitiveEquatable(value) {
        switch (typeof value) {
            case 'string':
            case 'number':
            case 'bigint':
            case 'boolean':
            case 'symbol':
                return true;
            default:
                return isBoxed(value);
        }
    }

    /**
     * @returns {EQUAL | NOT_EQUAL}
     */
    function comparePrimitiveEquality(a, b) {
        if (isBoxed(a)) a = a.valueOf();
        if (isBoxed(b)) b = b.valueOf();
        return tryCompareStrictEquality(a, b)
            || tryCompareTypeOfEquality(a, b)
            || compareIf(a, b, isNaNEquatable, compareNaNEquality)
            || NOT_EQUAL;
    }

    /**
     * @returns {value is number}
     */
    function isNaNEquatable(value) {
        return typeof value === 'number';
    }

    /**
     * @param {number} a
     * @param {number} b
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareNaNEquality(a, b) {
        return isNaN(a) && isNaN(b) ? EQUAL : NOT_EQUAL;
    }

    /**
     * @returns {value is object}
     */
    function isObjectEquatable(value) {
        return typeof value === 'object';
    }

    /**
     * @param {ComparisonCache} cache
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareObjectEquality(a, b, cache) {
        if (!cache) cache = new Map();
        return getCache(cache, a, b)
            || setCache(cache, a, b, EQUAL) // consider equal for now
            || cacheComparison(a, b, tryCompareStrictEquality, cache)
            || cacheComparison(a, b, tryCompareToStringTagEquality, cache)
            || compareIf(a, b, isValueOfEquatable, compareValueOfEquality)
            || compareIf(a, b, isToStringEquatable, compareToStringEquality)
            || compareIf(a, b, isArrayLikeEquatable, compareArrayLikeEquality, cache)
            || compareIf(a, b, isStructurallyEquatable, compareStructuralEquality, cache)
            || compareIf(a, b, isIterableEquatable, compareIterableEquality, cache)
            || cacheComparison(a, b, fail, cache);
    }

    function isBoxed(value) {
        return value instanceof String
            || value instanceof Number
            || value instanceof Boolean
            || typeof Symbol === 'function' && value instanceof Symbol
            || typeof BigInt === 'function' && value instanceof BigInt;
    }

    /**
     * @returns {value is { valueOf(): any }}
     */
    function isValueOfEquatable(value) {
        return value instanceof Date;
    }

    /**
     * @param {{ valueOf(): any }} a
     * @param {{ valueOf(): any }} b
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareValueOfEquality(a, b) {
        return compareIf(a.valueOf(), b.valueOf(), isPrimitiveEquatable, comparePrimitiveEquality)
            || NOT_EQUAL;
    }

    /**
     * @returns {value is { toString(): string }}
     */
    function isToStringEquatable(value) {
        return value instanceof RegExp;
    }

    /**
     * @param {{ toString(): string }} a
     * @param {{ toString(): string }} b
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareToStringEquality(a, b) {
        return compareIf(a.toString(), b.toString(), isPrimitiveEquatable, comparePrimitiveEquality)
            || NOT_EQUAL;
    }

    /**
     * @returns {value is ArrayLike<unknown>}
     */
    function isArrayLikeEquatable(value) {
        return (Array.isArray ? Array.isArray(value) : value instanceof Array)
            || (typeof Uint8Array === 'function' && value instanceof Uint8Array)
            || (typeof Uint8ClampedArray === 'function' && value instanceof Uint8ClampedArray)
            || (typeof Uint16Array === 'function' && value instanceof Uint16Array)
            || (typeof Uint32Array === 'function' && value instanceof Uint32Array)
            || (typeof Int8Array === 'function' && value instanceof Int8Array)
            || (typeof Int16Array === 'function' && value instanceof Int16Array)
            || (typeof Int32Array === 'function' && value instanceof Int32Array)
            || (typeof Float32Array === 'function' && value instanceof Float32Array)
            || (typeof Float64Array === 'function' && value instanceof Float64Array)
            || (typeof BigUint64Array === 'function' && value instanceof BigUint64Array)
            || (typeof BigInt64Array === 'function' && value instanceof BigInt64Array);
    }

    /**
     * @template T
     * @param {ArrayLike<T>} a
     * @param {ArrayLike<T>} b
     * @param {ComparisonCache} cache
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareArrayLikeEquality(a, b, cache) {
        if (a.length !== b.length) return NOT_EQUAL;
        for (var i = 0; i < a.length; i++) {
            if (compareEquality(a[i], b[i], cache) === NOT_EQUAL) {
                return NOT_EQUAL;
            }
        }
        return EQUAL;
    }

    /**
     * @template T
     * @param {T} value
     * @returns {value is Exclude<T, Promise | WeakMap | WeakSet | Map | Set>}
     */
    function isStructurallyEquatable(value) {
        return !(typeof Promise === 'function' && value instanceof Promise // only comparable by reference
            || typeof WeakMap === 'function' && value instanceof WeakMap // only comparable by reference
            || typeof WeakSet === 'function' && value instanceof WeakSet // only comparable by reference
            || typeof Map === 'function' && value instanceof Map // comparable via @@iterator
            || typeof Set === 'function' && value instanceof Set); // comparable via @@iterator
    }

    /**
     * @param {ComparisonCache} cache
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareStructuralEquality(a, b, cache) {
        var aKeys = [];
        for (var key in a) aKeys.push(key);

        var bKeys = [];
        for (var key in b) bKeys.push(key);

        if (aKeys.length !== bKeys.length) {
            return NOT_EQUAL;
        }

        aKeys.sort();
        bKeys.sort();

        for (var i = 0; i < aKeys.length; i++) {
            var aKey = aKeys[i];
            var bKey = bKeys[i];
            if (compareEquality(aKey, bKey, cache) === NOT_EQUAL) {
                return NOT_EQUAL;
            }
            if (compareEquality(a[aKey], b[bKey], cache) === NOT_EQUAL) {
                return NOT_EQUAL;
            }
        }

        return compareIf(a, b, isIterableEquatable, compareIterableEquality, cache)
            || EQUAL;
    }

    /**
     * @returns {value is Iterable<unknown>}
     */
    function isIterableEquatable(value) {
        return typeof Symbol === 'function'
            && typeof value[Symbol.iterator] === 'function';
    }

    /**
     * @template T
     * @param {Iterator<T>} a
     * @param {Iterator<T>} b
     * @param {ComparisonCache} cache
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareIteratorEquality(a, b, cache) {
        if (typeof Map === 'function' && a instanceof Map && b instanceof Map ||
            typeof Set === 'function' && a instanceof Set && b instanceof Set) {
            if (a.size !== b.size) return NOT_EQUAL; // exit early if we detect a difference in size
        }

        var ar, br;
        while (true) {
            ar = a.next();
            br = b.next();
            if (ar.done) {
                if (br.done) return EQUAL;
                if (b.return) b.return();
                return NOT_EQUAL;
            }
            if (br.done) {
                if (a.return) a.return();
                return NOT_EQUAL;
            }
            if (compareEquality(ar.value, br.value, cache) === NOT_EQUAL) {
                if (a.return) a.return();
                if (b.return) b.return();
                return NOT_EQUAL;
            }
        }
    }

    /**
     * @template T
     * @param {Iterable<T>} a
     * @param {Iterable<T>} b
     * @param {ComparisonCache} cache
     * @returns {EQUAL | NOT_EQUAL}
     */
    function compareIterableEquality(a, b, cache) {
        return compareIteratorEquality(a[Symbol.iterator](), b[Symbol.iterator](), cache);
    }

    /**
     * @template T
     * @template {EQUAL | NOT_EQUAL | UNKNOWN} R
     * @param {(a: T, b: T, circular?: ComparisonCache) => R} compare
     * @param {ComparisonCache} [cache]
     */
    function cacheComparison(a, b, compare, cache) {
        var result = compare(a, b, cache);
        if (cache && (result === EQUAL || result === NOT_EQUAL)) {
            setCache(cache, a, b, /** @type {EQUAL | NOT_EQUAL} */(result));
        }
        return result;
    }

    function fail() {
        return NOT_EQUAL;
    }

    /**
     * @param {EQUAL | NOT_EQUAL} result
     * @param {ComparisonCache} cache
     */
    function setCache(cache, left, right, result) {
        var otherCache;

        otherCache = cache.get(left);
        if (!otherCache) cache.set(left, otherCache = new Map());
        otherCache.set(right, result);

        otherCache = cache.get(right);
        if (!otherCache) cache.set(right, otherCache = new Map());
        otherCache.set(left, result);
    }

    /**
     * @param {ComparisonCache} cache
     */
    function getCache(cache, left, right) {
        var otherCache;
        /** @type {EQUAL | NOT_EQUAL | UNKNOWN | undefined} */
        var result;

        otherCache = cache.get(left);
        result = otherCache && otherCache.get(right);
        if (result) return result;

        otherCache = cache.get(right);
        result = otherCache && otherCache.get(left);
        if (result) return result;

        return UNKNOWN;
    }

    return deepEqual;
})();

/**
 * @template T
 * @param {T} actual
 * @param {T} expected
 * @param {string} [message]
 */
assert.deepEqual = function (actual, expected, message) {
    assert(deepEqual(actual, expected),
           'Expected ' + assert._formatValue(actual) + ' to be structurally equal to ' + assert._formatValue(expected) + '. ' + (message || ''));
};
