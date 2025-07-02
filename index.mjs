let count = 0;
const id = setInterval(() => {
    println("timeout: " + count)
    if (count == 10) {
        clearInterval(id)
    }
    count++
}, 1000)
println("start")