const obj = { item: 123 }
const data = Object.create(obj);
data[0] = 1
data[2] = 2
data[1] = 3
data.abc = "abc"
for (const key in data) {
    println(key)
}