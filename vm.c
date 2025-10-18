// main.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "opcode.h"   // make sure this provides OP_* byte constants

#define VM_CODE_LEN   1024
#define VM_CONSTS_LEN 512
#define VM_STACK_LEN  512
#define VM_BUF_LEN    4096   /* larger VM buffer for string concatenation */

/* value types */
typedef enum {
    NUMBER,
    STRING,
} vm_value_type;

typedef struct {
    vm_value_type type;
    union {
        double number;
        void *obj;
    } value;
} vm_value;

/* simple buffer for temporary strings */
typedef struct {
    char *buf;
    uint32_t bi; /* next free index */
} vm_buf;

typedef struct {
    char *code;      /* bytecode area */
    char *ip;        /* instruction pointer */
    vm_value *consts;
    uint32_t ci;     /* next const index / count */
    vm_value *stack;
    uint32_t si;     /* next stack index / count */
    vm_buf buf;
} vm_t;

/* Helpers/macros */
#define MAKE_NUMBER(n) ((vm_value){ .type = NUMBER, .value.number = (double)(n) })

/* read next byte from code as unsigned */
#define read_c() ((uint8_t)*vm->ip++)

/* Binary op: pop right then left, compute left <op> right, push number */
#define BINARY_OP(op) do {                        \
    vm_value v_right = vm_stack_pop(vm);          \
    vm_value v_left  = vm_stack_pop(vm);          \
    if (v_left.type != NUMBER || v_right.type != NUMBER) { \
        printf("TypeError: binary op on non-number\n"); \
        return (vm_value){0};                     \
    }                                             \
    double res = v_left.value.number op v_right.value.number; \
    vm_stack_push(vm, MAKE_NUMBER(res));          \
} while (0)

/* Forward declarations */
void vm_init(vm_t *vm);
void vm_free(vm_t *vm);
void vm_push_consts(vm_t *vm, vm_value value);
void vm_stack_push(vm_t *vm, vm_value value);
vm_value vm_stack_pop(vm_t *vm);
char *vm_bufcat(vm_t *vm, vm_value v1, vm_value v2);
vm_value vm_eval(vm_t *vm);
vm_value vm_exec(vm_t *vm, char *prog);

/* Implementation */

void vm_init(vm_t *vm) {
    vm->code    = (char*) malloc(VM_CODE_LEN);
    if (!vm->code) { perror("malloc code"); exit(1); }
    vm->ip      = vm->code;
    vm->consts  = (vm_value*) malloc(sizeof(vm_value) * VM_CONSTS_LEN);
    if (!vm->consts) { perror("malloc consts"); exit(1); }
    vm->stack   = (vm_value*) malloc(sizeof(vm_value) * VM_STACK_LEN);
    if (!vm->stack) { perror("malloc stack"); exit(1); }

    vm->buf.buf = (char*) malloc(VM_BUF_LEN);
    if (!vm->buf.buf) { perror("malloc buf"); exit(1); }
    vm->buf.bi = 0;

    vm->ci = 0;
    vm->si = 0;
}

/* push a constant into const table */
void vm_push_consts(vm_t *vm, vm_value value) {
    if (vm->ci >= VM_CONSTS_LEN) {
        printf("constant index out of range %u\n", vm->ci);
        return;
    }
    vm->consts[vm->ci++] = value;
}

/* push to VM stack */
void vm_stack_push(vm_t *vm, vm_value value) {
    if (vm->si >= VM_STACK_LEN) {
        printf("stack index out of range %u\n", vm->si);
        return;
    }
    vm->stack[vm->si++] = value;
}

/* pop from VM stack */
vm_value vm_stack_pop(vm_t *vm) {
    if (vm->si == 0) {
        printf("Invalid stack pop: stack empty\n");
        return (vm_value){0};
    }
    return vm->stack[--vm->si];
}

/* Safe concatenation: copy v1 and v2 into vm->buf, return pointer to the concatenation
   v1 and v2 are expected to be STRING typed and their .value.obj point to NUL-terminated strings.
   We do NOT modify the original strings. */
char *vm_bufcat(vm_t *vm, vm_value v1, vm_value v2) {
    if (v1.type != STRING || v2.type != STRING) {
        return NULL;
    }
    const char *s1 = (const char *) v1.value.obj;
    const char *s2 = (const char *) v2.value.obj;
    size_t l1 = strlen(s1);
    size_t l2 = strlen(s2);
    if (l1 + l2 + 1 > VM_BUF_LEN) {
        printf("vm buffer overflow: need %zu but have %u\n", l1 + l2 + 1, VM_BUF_LEN);
        return NULL;
    }

    /* If not enough room at the end of our rolling buffer, reset to 0 for simplicity */
    if (vm->buf.bi + l1 + l2 + 1 > VM_BUF_LEN) {
        vm->buf.bi = 0;
    }
    char *dest = vm->buf.buf + vm->buf.bi;
    memcpy(dest, s1, l1);
    memcpy(dest + l1, s2, l2);
    dest[l1 + l2] = '\0';
    vm->buf.bi += (uint32_t)(l1 + l2 + 1);
    return dest;
}

vm_value vm_eval(vm_t *vm) {
    /* instruction pointer must point to start of code */
    vm->ip = vm->code;
    for (;;) {
        uint8_t c = read_c();
        switch (c) {
            case OP_HALT: {
                return vm_stack_pop(vm);
            }

            case OP_CONST: {
                uint8_t ci = read_c();
                if (ci >= vm->ci) {
                    printf("Invalid const index %u\n", ci);
                    return (vm_value){0};
                }
                vm_value cv = vm->consts[ci];
                vm_stack_push(vm, cv);
            } break;

            case OP_ADD: {
                vm_value right = vm_stack_pop(vm);
                vm_value left  = vm_stack_pop(vm);
                if (!(left.type ^ right.type)) {
                    double res = left.value.number + right.value.number;
                    vm_stack_push(vm, MAKE_NUMBER(res));
                } else if (!(left.type ^ right.type)) {
                    vm_value val;
                    char *s = vm_bufcat(vm, left, right);
                    if (!s) {
                        printf("String concat failed\n");
                        return (vm_value){0};
                    }
                    val.type = STRING;
                    val.value.obj = s;
                    vm_stack_push(vm, val);
                    /* debug print */
                    // printf("[DEBUG] %s\n", s);
                } else {
                    printf("TypeError: unsupported types for add\n");
                    return (vm_value){0};
                }
            } break;

            case OP_SUB:
                BINARY_OP(-);
                break;

            case OP_MUL:
                BINARY_OP(*);
                break;

            case OP_DIV:
                BINARY_OP(/);
                break;

            default:
                printf("unknown opcode %u\n", (unsigned)c);
                return (vm_value){0};
        }
    }
}

/* Create a small test program and run it */
vm_value vm_exec(vm_t *vm, char *prog) {
    /* fill constants (indices 0,1) */
    vm_value v0, v1;
    v0.type = STRING; v0.value.obj = "HELLO ";
    v1.type = STRING; v1.value.obj = "WORLD";
    vm_push_consts(vm, v0); /* const 0 */
    vm_push_consts(vm, v1); /* const 1 */

    /* simple test bytecode: push const 0, push const 1, add (concat), halt */
    uint8_t test_code[] = { OP_CONST, 0, OP_CONST, 1, OP_ADD, OP_HALT };

    /* write bytecode into vm->code */
    size_t len = sizeof(test_code);
    if (len > VM_CODE_LEN) {
        printf("program too large\n");
        return (vm_value){0};
    }
    memcpy(vm->code, test_code, len);

    /* reset ip and evaluate */
    return vm_eval(vm);
}

void vm_free(vm_t *vm) {
    if (vm->code) free(vm->code);
    if (vm->consts) free(vm->consts);
    if (vm->stack) free(vm->stack);
    if (vm->buf.buf) free(vm->buf.buf);
}

int32_t main(int argc, char *argv[]) {
    vm_t vm = {0};
    vm_init(&vm);

    vm_value val = vm_exec(&vm, NULL);
    switch (val.type) {
        case NUMBER:
            printf("[VM_LOG] %f\n", val.value.number);
            break;
        case STRING:
            printf("[VM_LOG] %s\n", (char*) val.value.obj);
            break;
        default:
            printf("[VM_LOG] <no result or invalid>\n");
    }

    vm_free(&vm);
    return 0;
}