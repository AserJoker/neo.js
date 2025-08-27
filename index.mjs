'use strict'
let obj = null
const init = () => {
    const o = {}
    obj = o;
    o.data = 123;
}
init();
console.log(obj)