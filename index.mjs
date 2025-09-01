'use strict'
const fn = (...args) => console.log(args[0][0])
fn`a\nb${123}`