'use strict'
const { a, ...b } = { a: 123, b: 234, c: 345 }
print(a, b.b, b.c)