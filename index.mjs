"use strict";
const obj = {};
Object.defineProperty(obj, "data", {
  set: (val) => {},
});
console.log(obj.data);
