'use strict';
const sym = Symbol();
const obj = {
    [sym]: 123
}
console.log(Object.keys(obj))