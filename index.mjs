'use strict'
async function* gen() {
    yield Promise.reject(234)
    return 123
}
const g = gen()
g.next()