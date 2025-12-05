'use strict'
async function* gen() {
    try {
        await new Promise((_, reject) => {
            reject(new Error('test error'))
        })
    } catch (e) {
        print(e.message);
    }
}
const g = gen()
g.next().then(val => print(val))
g.next().then(val => print(val))