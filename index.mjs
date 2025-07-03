function getA(val) {
    return new Promise((resolve) => {
        setTimeout(() => {
            resolve(val + 1)
        }, 1000);
    })
}
getA(10).then(val => println(val))