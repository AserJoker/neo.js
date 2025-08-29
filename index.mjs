'use strict'
let obj = null
const fn = () => {
    obj = {}
    obj.data = 123
}
const get = () => obj.data;
fn()
console.log(get())