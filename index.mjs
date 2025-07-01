const { a, b, ...c } = { a: 123, b: 234, data: 'test', [Symbol.toStringTag]: 'Test' };
println(a, b, c)