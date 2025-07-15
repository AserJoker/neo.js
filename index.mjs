class Test {
    #data = 123
    print() {
        console.log(this.#data)
    }
};
new Test().print()