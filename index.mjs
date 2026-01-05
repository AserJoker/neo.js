async function* agen() {
    yield Promise.resolve(1)
    yield 2
    yield 3
}
const gen = agen();
Array.fromAsync(gen, (val) => val + 1).then((val) => console.log(val))