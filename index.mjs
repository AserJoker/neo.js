'use strict'
class Base {
    #data = 234
    write() {
        print(this.#data)
    }
};
new Base().write()
