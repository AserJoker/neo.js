'use strict'
const pro = new Promise((resolve, reject) => {
    throw new Error('test')
})
pro.catch(e => print(e))
print(234)