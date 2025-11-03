"use strict";

const obj = {};
Object.defineProperty(obj, "val", {
  configurable: true,
  enumerable: true,
  value: 123,
});
Object.freeze(obj)
delete obj.val;
console.log(obj);
