[section .metadata]
.file: ./index.mjs
[section .constants]
.0: "test"
.1: "function test() {\r\n    return 123;\r\n}"
.2: "println"
[section .data]
0: NEO_ASM_PUSH_SCOPE
2: NEO_ASM_PUSH_FUNCTION
4: NEO_ASM_PUSH_STRING "test"
14: NEO_ASM_SET_NAME
16: NEO_ASM_SET_SOURCE "function test() {\r\n    return 123;\r\n}"
26: NEO_ASM_SET_ADDRESS 94
36: NEO_ASM_DEF "test"
46: NEO_ASM_LOAD "test"
56: NEO_ASM_POP
58: NEO_ASM_LOAD "println"
68: NEO_ASM_LOAD "test"
78: NEO_ASM_CALL 0
84: NEO_ASM_CALL 1
90: NEO_ASM_POP
92: NEO_ASM_HLT
94: NEO_ASM_PUSH_SCOPE
96: NEO_ASM_PUSH_NUMBER 123.000000
106: NEO_ASM_RET
108: NEO_ASM_PUSH_UNDEFINED
110: NEO_ASM_RET
112: NEO_ASM_POP_SCOPE
114: NEO_ASM_POP_SCOPE
