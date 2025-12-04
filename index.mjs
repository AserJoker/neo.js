'use strict'
new Promise((resolve) => {
    resolve()
}).then(() => {
    throw new Error('test')
}).catch(e => print(e.message))