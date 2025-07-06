const it = (function* () {
  println(123);
  yield 1;
  println(234);
})();
it.next();
it.next();
it.next();
it.next();
