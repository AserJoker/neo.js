'use strict'
class Base {
    get _data() {
        print('-----------')
    }
    write() {
        print('base')
    }
};
class Test extends Base {
    _data = 123
    write() {
        super.write()
        print('test')
    }
};
new Test().write()
