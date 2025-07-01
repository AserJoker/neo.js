try {
    try {
        throw "Test error"
    } finally {
        println("finally")
    }
} catch (e) {
    println("catch" + e)
}