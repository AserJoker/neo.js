[section .metadata]
.file: index.mjs
[section .constants]
.0: "test"
.1: "async function test() {\r\n  try {\r\n    aaa: {\r\n      await using obj = {\r\n        async [Symbol.asyncDispose]() {\r\n          await new Promise(resolve => setTimeout(resolve, 1000))\r\n          throw new Error('test')\r\n        }\r\n      }\r\n      break aaa\r\n    }\r\n  } catch (e) {\r\n    println(e)\r\n  }\r\n}"
.2: "item"
.3: "Symbol"
.4: "Promise"
.5: "setTimeout"
.6: "Error"
.7: "println"
.8: "length"
.9: "aaa"
.10: "obj"
.11: "asyncDispose"
.12: "resolve"
.13: "arguments"
.14: "resolve => setTimeout(resolve, 1000)"
.15: "async [Symbol.asyncDispose]() {\r\n          await new Promise(resolve => setTimeout(resolve, 1000))\r\n          throw new Error('test')\r\n        }"
.16: "e"
[section .data]
0: NEO_ASM_PUSH_SCOPE
2: NEO_ASM_PUSH_ASYNC_FUNCTION
4: NEO_ASM_SET_NAME "test"
14: NEO_ASM_SET_SOURCE "async function test() {\r\n  try {\r\n    aaa: {\r\n      await using obj = {\r\n        async [Symbol.asyncDispose]() {\r\n          await new Promise(resolve => setTimeout(resolve, 1000))\r\n          throw new Error('test')\r\n        }\r\n      }\r\n      break aaa\r\n    }\r\n  } catch (e) {\r\n    println(e)\r\n  }\r\n}"
24: NEO_ASM_SET_ADDRESS 226
34: NEO_ASM_DEF "test"
44: NEO_ASM_PUSH_UNINITIALIZED
46: NEO_ASM_SET_CONST
48: NEO_ASM_DEF "item"
58: NEO_ASM_LOAD "test"
68: NEO_ASM_SET_CLOSURE "Symbol"
78: NEO_ASM_SET_CLOSURE "Promise"
88: NEO_ASM_SET_CLOSURE "setTimeout"
98: NEO_ASM_SET_CLOSURE "Error"
108: NEO_ASM_SET_CLOSURE "println"
118: NEO_ASM_POP
120: NEO_ASM_LOAD "test"
130: NEO_ASM_PUSH_ARRAY 0
140: NEO_ASM_CALL 16,20
150: NEO_ASM_AWAIT
152: NEO_ASM_STORE "item"
162: NEO_ASM_LOAD "println"
172: NEO_ASM_PUSH_ARRAY 0
182: NEO_ASM_PUSH_VALUE 1
188: NEO_ASM_PUSH_STRING "length"
198: NEO_ASM_GET_FIELD
200: NEO_ASM_LOAD "item"
210: NEO_ASM_SET_FIELD
212: NEO_ASM_CALL 17,1
222: NEO_ASM_POP
224: NEO_ASM_HLT
226: NEO_ASM_PUSH_SCOPE
228: NEO_ASM_PUSH_SCOPE
230: NEO_ASM_TRY_BEGIN 736,0
248: NEO_ASM_PUSH_SCOPE
250: NEO_ASM_PUSH_BREAK_LABEL "aaa",722
268: NEO_ASM_PUSH_CONTINUE_LABEL "aaa",720
286: NEO_ASM_PUSH_SCOPE
288: NEO_ASM_PUSH_UNINITIALIZED
290: NEO_ASM_SET_AWAIT_USING
292: NEO_ASM_DEF "obj"
302: NEO_ASM_PUSH_OBJECT
304: NEO_ASM_LOAD "Symbol"
314: NEO_ASM_PUSH_STRING "asyncDispose"
324: NEO_ASM_GET_FIELD
326: NEO_ASM_JMP 644
336: NEO_ASM_PUSH_SCOPE
338: NEO_ASM_PUSH_SCOPE
340: NEO_ASM_LOAD "Promise"
350: NEO_ASM_PUSH_ARRAY 0
360: NEO_ASM_PUSH_VALUE 1
366: NEO_ASM_PUSH_STRING "length"
376: NEO_ASM_GET_FIELD
378: NEO_ASM_JMP 526
388: NEO_ASM_PUSH_SCOPE
390: NEO_ASM_PUSH_UNDEFINED
392: NEO_ASM_DEF "resolve"
402: NEO_ASM_LOAD "arguments"
412: NEO_ASM_ITERATOR
414: NEO_ASM_NEXT
416: NEO_ASM_POP
418: NEO_ASM_STORE "resolve"
428: NEO_ASM_POP
430: NEO_ASM_POP
432: NEO_ASM_LOAD "setTimeout"
442: NEO_ASM_PUSH_ARRAY 0
452: NEO_ASM_PUSH_VALUE 1
458: NEO_ASM_PUSH_STRING "length"
468: NEO_ASM_GET_FIELD
470: NEO_ASM_LOAD "resolve"
480: NEO_ASM_SET_FIELD
482: NEO_ASM_PUSH_VALUE 1
488: NEO_ASM_PUSH_STRING "length"
498: NEO_ASM_GET_FIELD
500: NEO_ASM_PUSH_NUMBER 1000
510: NEO_ASM_SET_FIELD
512: NEO_ASM_CALL 6,40
522: NEO_ASM_RET
524: NEO_ASM_POP_SCOPE
526: NEO_ASM_PUSH_LAMBDA
528: NEO_ASM_SET_ADDRESS 388
538: NEO_ASM_SET_SOURCE "resolve => setTimeout(resolve, 1000)"
548: NEO_ASM_SET_CLOSURE "setTimeout"
558: NEO_ASM_SET_FIELD
560: NEO_ASM_NEW 6,17
570: NEO_ASM_AWAIT
572: NEO_ASM_POP
574: NEO_ASM_LOAD "Error"
584: NEO_ASM_PUSH_ARRAY 0
594: NEO_ASM_PUSH_VALUE 1
600: NEO_ASM_PUSH_STRING "length"
610: NEO_ASM_GET_FIELD
612: NEO_ASM_PUSH_STRING "test"
622: NEO_ASM_SET_FIELD
624: NEO_ASM_NEW 7,17
634: NEO_ASM_THROW
636: NEO_ASM_PUSH_UNDEFINED
638: NEO_ASM_RET
640: NEO_ASM_POP_SCOPE
642: NEO_ASM_POP_SCOPE
644: NEO_ASM_PUSH_ASYNC_FUNCTION
646: NEO_ASM_SET_ADDRESS 336
656: NEO_ASM_SET_SOURCE "async [Symbol.asyncDispose]() {\r\n          await new Promise(resolve => setTimeout(resolve, 1000))\r\n          throw new Error('test')\r\n        }"
666: NEO_ASM_SET_CLOSURE "Promise"
676: NEO_ASM_SET_CLOSURE "setTimeout"
686: NEO_ASM_SET_CLOSURE "Error"
696: NEO_ASM_SET_METHOD
698: NEO_ASM_STORE "obj"
708: NEO_ASM_BREAK "aaa"
718: NEO_ASM_POP_SCOPE
720: NEO_ASM_POP_LABEL
722: NEO_ASM_POP_LABEL
724: NEO_ASM_POP_SCOPE
726: NEO_ASM_JMP 828
736: NEO_ASM_PUSH_SCOPE
738: NEO_ASM_PUSH_UNINITIALIZED
740: NEO_ASM_DEF "e"
750: NEO_ASM_STORE "e"
760: NEO_ASM_PUSH_SCOPE
762: NEO_ASM_LOAD "println"
772: NEO_ASM_PUSH_ARRAY 0
782: NEO_ASM_PUSH_VALUE 1
788: NEO_ASM_PUSH_STRING "length"
798: NEO_ASM_GET_FIELD
800: NEO_ASM_LOAD "e"
810: NEO_ASM_SET_FIELD
812: NEO_ASM_CALL 13,5
822: NEO_ASM_POP
824: NEO_ASM_POP_SCOPE
826: NEO_ASM_POP_SCOPE
828: NEO_ASM_TRY_END
830: NEO_ASM_PUSH_UNDEFINED
832: NEO_ASM_RET
834: NEO_ASM_POP_SCOPE
836: NEO_ASM_POP_SCOPE
838: NEO_ASM_POP_SCOPE
