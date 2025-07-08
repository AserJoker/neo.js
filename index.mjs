async function* getArr() {
  yield* ['a', 'b', 'c'];
}
for await (const item of getArr()) {
  println(item)
}