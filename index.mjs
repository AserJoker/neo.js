await using obj = {
  async [Symbol.asyncDispose]() {
    await new Promise(resolve => setTimeout(resolve))
    throw new Error('test')
  }
}
