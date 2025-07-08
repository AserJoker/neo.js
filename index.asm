[section .metadata]
.file: index.mjs
[section .constants]
.0: "test"
.1: "function test() {\r\n  using obj = {\r\n    [Symbol.dispose]() {\r\n      println('dispose')\r\n    }\r\n  }\r\n  println(obj[Symbol.dispose])\r\n}"
.2: "Symbol"
.3: "println"
.4: "obj"
.5: "dispose"
.6: "length"
.7: "[Symbol.dispose]() {\r\n      println('dispose')\r\n    }"
[section .data]
0: NEO_ASM_PUSH_SCOPE
2: NEO_ASM_PUSH_FUNCTION
4: NEO_ASM_SET_NAME "test"
14: NEO_ASM_SET_SOURCE "function test() {\r\n  using obj = {\r\n    [Symbol.dispose]() {\r\n      println('dispose')\r\n    }\r\n  }\r\n  println(obj[Symbol.dispose])\r\n}"
24: NEO_ASM_SET_ADDRESS 110
34: NEO_ASM_DEF "test"
44: NEO_ASM_LOAD "test"
54: NEO_ASM_SET_CLOSURE "Symbol"
64: NEO_ASM_SET_CLOSURE "println"
74: NEO_ASM_POP
76: NEO_ASM_LOAD "test"
86: NEO_ASM_PUSH_ARRAY 0
96: NEO_ASM_CALL 9,1
106: NEO_ASM_POP
108: NEO_ASM_HLT
110: NEO_ASM_PUSH_SCOPE
112: NEO_ASM_PUSH_SCOPE
114: NEO_ASM_PUSH_UNINITIALIZED
116: NEO_ASM_SET_USING
118: NEO_ASM_DEF "obj"
128: NEO_ASM_PUSH_OBJECT
130: NEO_ASM_LOAD "Symbol"
140: NEO_ASM_PUSH_STRING "dispose"
150: NEO_ASM_GET_FIELD
152: NEO_ASM_JMP 236
162: NEO_ASM_PUSH_SCOPE
164: NEO_ASM_PUSH_SCOPE
166: NEO_ASM_LOAD "println"
176: NEO_ASM_PUSH_ARRAY 0
186: NEO_ASM_PUSH_VALUE 1
192: NEO_ASM_PUSH_STRING "length"
202: NEO_ASM_GET_FIELD
204: NEO_ASM_PUSH_STRING "dispose"
214: NEO_ASM_SET_FIELD
216: NEO_ASM_CALL 4,7
226: NEO_ASM_POP
228: NEO_ASM_PUSH_UNDEFINED
230: NEO_ASM_RET
232: NEO_ASM_POP_SCOPE
234: NEO_ASM_POP_SCOPE
236: NEO_ASM_PUSH_FUNCTION
238: NEO_ASM_SET_ADDRESS 162
248: NEO_ASM_SET_SOURCE "[Symbol.dispose]() {\r\n      println('dispose')\r\n    }"
258: NEO_ASM_SET_CLOSURE "println"
268: NEO_ASM_SET_METHOD
270: NEO_ASM_STORE "obj"
280: NEO_ASM_LOAD "println"
290: NEO_ASM_PUSH_ARRAY 0
300: NEO_ASM_PUSH_VALUE 1
306: NEO_ASM_PUSH_STRING "length"
316: NEO_ASM_GET_FIELD
318: NEO_ASM_LOAD "obj"
328: NEO_ASM_LOAD "Symbol"
338: NEO_ASM_PUSH_STRING "dispose"
348: NEO_ASM_GET_FIELD
350: NEO_ASM_GET_FIELD
352: NEO_ASM_SET_FIELD
354: NEO_ASM_CALL 7,3
364: NEO_ASM_POP
366: NEO_ASM_PUSH_UNDEFINED
368: NEO_ASM_RET
370: NEO_ASM_POP_SCOPE
372: NEO_ASM_POP_SCOPE
374: NEO_ASM_POP_SCOPE
