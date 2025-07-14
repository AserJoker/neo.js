class Test {
  set #item(val) {
    this.item = val;
  }
  get #item() {
    return this.item;
  }
  print() {
    console.log(this.#item);
  }
  write(val) {
    this.#item = val;
  }
};

const item = new Test();
item.write(123)
item.print()