'use strict'
const fn = async (pro) => await pro;
fn(Promise.resolve(123)).then(val => print(val))