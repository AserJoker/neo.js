const fn = () => {
  return new Promise((_, reject) => reject(new Error('test')));
}
async function test() {
  try {
  } catch (e) {
    println(e);
    return 2;
  }
  return 3
}
test().then(println)