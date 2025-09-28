'use strict'
let obj = null
const fn = () => {
    console.log(obj.data)
}
obj = {
    data: 123
}
fn()