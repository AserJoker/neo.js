async function* getArr() {
  await println('123');
  yield* ['a', 'b', 'c']
  await println('234')
}

for await (const item of getArr()) {
  println(item)
}