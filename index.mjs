const arr = ['a', 'b', 'c', 'd']
for (const i of arr) {
    if (i == 'c') {
        continue
    }
    println(i)
}