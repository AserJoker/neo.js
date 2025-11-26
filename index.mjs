'use strict'
const obj = {
    get data() {
        return this._data;
    },
    set data(val) {
        this._data = val;
    },
    print() {
        print(this.data)
    }
}
obj.print()
obj.data = 234
obj.print()