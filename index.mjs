function* get() {
  try {
    yield 1;
    yield 2;
    yield 3;
  } catch {
    return 4;
  }
}

const iterator = get();
iterator.next();
println(123);
const gen = iterator.throw(4);
println(gen.value);
