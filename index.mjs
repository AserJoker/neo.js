'use strict'
class Base {
    accessor #data = 234
    #write() {
        print(this.#data)
    }
    write() {
        this.#write()
    }
};
class Test extends Base { };
new Test().write()