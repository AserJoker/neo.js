[section .metadata]
.filename: /home/aserjoker/neo.js/index.mjs
.dirname: /home/aserjoker/neo.js/index.mjs
[section .constants]
.0: "obj"
.1: "Symbol"
.2: "asyncDispose"
.3: "Promise"
.4: "length"
.5: "resolve"
.6: "arguments"
.7: "setTimeout"
.8: "resolve => setTimeout(resolve)"
.9: "Error"
.10: "test"
.11: "async [Symbol.asyncDispose]() {\r\n    await new Promise(resolve => setTimeout(resolve))\r\n    throw new Error('test')\r\n  }"
[section .data]
0: NEO_ASM_PUSH_SCOPE
2: NEO_ASM_PUSH_UNINITIALIZED
4: NEO_ASM_SET_AWAIT_USING
6: NEO_ASM_DEF "obj"
16: NEO_ASM_PUSH_OBJECT
18: NEO_ASM_LOAD "Symbol"
28: NEO_ASM_PUSH_STRING "asyncDispose"
38: NEO_ASM_GET_FIELD
40: NEO_ASM_JMP 328
50: NEO_ASM_PUSH_SCOPE
52: NEO_ASM_PUSH_SCOPE
54: NEO_ASM_LOAD "Promise"
64: NEO_ASM_PUSH_ARRAY 0
74: NEO_ASM_PUSH_VALUE 1
80: NEO_ASM_PUSH_STRING "length"
90: NEO_ASM_GET_FIELD
92: NEO_ASM_JMP 210
102: NEO_ASM_PUSH_SCOPE
104: NEO_ASM_PUSH_UNDEFINED
106: NEO_ASM_DEF "resolve"
116: NEO_ASM_LOAD "arguments"
126: NEO_ASM_ITERATOR
128: NEO_ASM_NEXT
130: NEO_ASM_POP
132: NEO_ASM_STORE "resolve"
142: NEO_ASM_POP
144: NEO_ASM_POP
146: NEO_ASM_LOAD "setTimeout"
156: NEO_ASM_PUSH_ARRAY 0
166: NEO_ASM_PUSH_VALUE 1
172: NEO_ASM_PUSH_STRING "length"
182: NEO_ASM_GET_FIELD
184: NEO_ASM_LOAD "resolve"
194: NEO_ASM_SET_FIELD
196: NEO_ASM_CALL 3,34
206: NEO_ASM_RET
208: NEO_ASM_POP_SCOPE
210: NEO_ASM_PUSH_LAMBDA
212: NEO_ASM_SET_ADDRESS 102
222: NEO_ASM_SET_SOURCE "resolve => setTimeout(resolve)"
232: NEO_ASM_SET_CLOSURE "setTimeout"
242: NEO_ASM_SET_FIELD
244: NEO_ASM_NEW 3,11
254: NEO_ASM_AWAIT
256: NEO_ASM_POP
258: NEO_ASM_LOAD "Error"
268: NEO_ASM_PUSH_ARRAY 0
278: NEO_ASM_PUSH_VALUE 1
284: NEO_ASM_PUSH_STRING "length"
294: NEO_ASM_GET_FIELD
296: NEO_ASM_PUSH_STRING "test"
306: NEO_ASM_SET_FIELD
308: NEO_ASM_NEW 4,11
318: NEO_ASM_THROW
320: NEO_ASM_PUSH_UNDEFINED
322: NEO_ASM_RET
324: NEO_ASM_POP_SCOPE
326: NEO_ASM_POP_SCOPE
328: NEO_ASM_PUSH_ASYNC_FUNCTION
330: NEO_ASM_SET_ADDRESS 50
340: NEO_ASM_SET_SOURCE "async [Symbol.asyncDispose]() {\r\n    await new Promise(resolve => setTimeout(resolve))\r\n    throw new Error('test')\r\n  }"
350: NEO_ASM_SET_CLOSURE "Promise"
360: NEO_ASM_SET_CLOSURE "setTimeout"
370: NEO_ASM_SET_CLOSURE "Error"
380: NEO_ASM_SET_METHOD
382: NEO_ASM_STORE "obj"
392: NEO_ASM_HLT
394: NEO_ASM_POP_SCOPE
