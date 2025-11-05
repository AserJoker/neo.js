"use strict";

const obj = {};
obj[Symbol.toPrimitive] = () => obj;

console.log(`${obj}`)