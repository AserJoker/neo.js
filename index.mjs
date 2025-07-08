async function test() {
  println(234)
  throw new Error('test')
}
println(123)
await test()
println(345)