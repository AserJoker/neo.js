print('aaaaa')
await Promise.resolve()
print('bbbb')
await new Promise((resolve) => setTimeout(resolve, 1000))
print('cccc')
export const add = (a, b) => a + b