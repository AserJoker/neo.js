'use strict'
const fn = () => {
    try {
        {
            using item = {
                [Symbol.dispose]() {
                    throw new Error('test error')
                }
            }
            return;
        }
    } catch (e) {
        console.log(e)
    }
}
fn()