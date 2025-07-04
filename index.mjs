async function getA(val) {
    const val2 = await new Promise((resolve) => {
        setTimeout(() => resolve(val + 1));
    });
    return val2;
}
getA(11).then(println)