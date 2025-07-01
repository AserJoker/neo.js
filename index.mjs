const obj = {
    _data: 123,
    get data() {
        return this._data;
    },
    set data(val) {
        this._data = val;
    }
}
obj.data = "hello world"
println(obj.data, obj._data)