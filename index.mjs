'use strict'
const fn = async () => {
    try {
        aaaaa: {
            await using item2 = {
                async [Symbol.asyncDispose]() {
                    console.log('item2')
                }
            }
            await using item3 = {
                async [Symbol.asyncDispose]() {
                    console.log('item3')
                }
            }
            {
                return 123;
            }
        }
    }
    catch (e) {
        console.log(e);
        console.log(e.error);
        console.log(e.suppressed);
    } finally {
        console.log('dispose3')
    }
}
fn().then(res => console.log(res));