'use strict'
function* test() {
    try {
        yield 123
    } catch (e) {
        print('error:' + e)
    }
}
const gen = test();
print(gen.next().value)
gen.throw('test error').done