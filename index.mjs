function test() {
  aaa: {
    println('123')
    try {
      using obj = {
        [Symbol.dispose]() {
          throw new Error('test')
        }
      }
      break aaa;
    } catch (e) {
      println(e)
    } finally {
      println('234')
    }
  }
}
test()