const it = (function* () {
  println(123);
  yield 1;
  println(234);
  println(it)
})();
it.next();
