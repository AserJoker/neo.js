"use strict";
try {
    try {
        throw 'error'
    } finally {
        print('end')
    }
} catch (e) {
    print(e)
}