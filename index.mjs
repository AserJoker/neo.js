const pro = new Promise((resolve) => {
    resolve(123);
})
pro.then((val) => {
    println(val);
})