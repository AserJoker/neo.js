'use strict'
const pro = new Promise((resolve) => {
    resolve(123)
})
pro.then((val) => print(val))
print(234)