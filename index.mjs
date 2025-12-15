'use strict'
const fn = async () => {
    {
        await using item = {
            async [Symbol.asyncDispose]() {
                console.log('aaa')
            }
        }
        await using item2 = {
            async [Symbol.asyncDispose]() {
                console.log('bbb')
            }
        }
    }
    console.log('ccc')
}
fn()