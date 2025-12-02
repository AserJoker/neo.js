'use strict'
const pro = new Promise((resolve) => {
    setTimeout(() => {
        resolve(123);
    }, 1000)
})
Promise.resolve(pro).then((val) => print(val))