'use strict'
async function* test() {
    yield 1
    yield new Promise(resolve => setTimeout(() => resolve(2), 100))
    yield 3
}
Array.fromAsync(test()).then(res => console.log(res))