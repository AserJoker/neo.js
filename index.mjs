'use strict'
const obj = [1, 2, 3]
obj['data'] = 123
obj['aaa'] = 123
for (const item in obj) {
    print(item)
}