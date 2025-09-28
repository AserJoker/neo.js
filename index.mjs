'use strict'
const obj = {};
const proxy = new Proxy(obj, {
    get(target, key) {
        if (key == "data") {
            return 123;
        }
        return target[key];
    }
})
console.log(proxy.data)