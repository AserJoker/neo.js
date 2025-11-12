"use strict";
const obj = {
  length: 9_007_199_254_740_991,
};
Array.prototype.push.call(obj,1)
console.log(obj)