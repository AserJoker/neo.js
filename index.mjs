function test() {
  try {
    using obj = {
      [Symbol.dispose]() {
        throw new Error('test')
      }
    }
  } catch (e) {
    console.log(e)
  }
}
test();