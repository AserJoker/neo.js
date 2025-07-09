async function test() {
  try {
    aaa: {
      await using obj = {
        async [Symbol.asyncDispose]() {
          await new Promise(resolve => setTimeout(resolve, 1000))
          throw new Error('test')
        }
      }
      break aaa
    }
  } catch (e) {
    println(e)
  }
}
const item = await test()
println(item)