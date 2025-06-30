[section .metadata]
.file: index.mjs
[section .constants]
.0: "println"
.1: "999"
.2: "a"
[section .data]
0: NEO_ASM_PUSH_SCOPE
2: NEO_ASM_LOAD "println"
12: NEO_ASM_PUSH_STRING "999"
22: NEO_ASM_PUSH_STRING "a"
32: NEO_ASM_LT
34: NEO_ASM_CALL 1
40: NEO_ASM_POP
42: NEO_ASM_HLT
44: NEO_ASM_POP_SCOPE
