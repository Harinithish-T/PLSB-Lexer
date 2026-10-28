/* =========================================================================================
 * Real-World C Benchmark Dataset: realworld_c_sample.c
 * Provenance: Synthesized representative C systems code modeled after SQLite3 and Lua VM
 * internals, containing lexically realistic distributions of identifiers, keywords,
 * operators, comments, numeric literals, and structured control flow.
 * =========================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_OPCODES 256
#define REGISTER_COUNT 128
#define INSTRUCTION_MASK 0x00FFFFFF

typedef enum {
    OP_NOOP = 0,
    OP_LOAD_CONST,
    OP_MOVE_REG,
    OP_ADD_INT,
    OP_SUB_INT,
    OP_MUL_INT,
    OP_DIV_INT,
    OP_JUMP_IF_ZERO,
    OP_JUMP_ALWAYS,
    OP_CALL_FUNC,
    OP_RETURN_VAL,
    OP_HALT
} vm_opcode_t;

typedef struct {
    uint8_t opcode;
    uint8_t reg_dest;
    uint8_t reg_src1;
    uint8_t reg_src2;
    int32_t immediate_operand;
} vm_instruction_t;

typedef struct {
    int64_t registers[REGISTER_COUNT];
    int64_t *stack_memory;
    size_t stack_capacity;
    size_t stack_pointer;
    uint32_t program_counter;
    bool execution_halted;
    uint64_t total_cycles_executed;
} vm_virtual_machine_state_t;

static inline int vm_push_stack(vm_virtual_machine_state_t *vm, int64_t value) {
    if (vm->stack_pointer >= vm->stack_capacity) {
        size_t new_cap = vm->stack_capacity ? (vm->stack_capacity * 2) : 64;
        int64_t *new_mem = (int64_t *)realloc(vm->stack_memory, new_cap * sizeof(int64_t));
        if (!new_mem) {
            return -1;
        }
        vm->stack_memory = new_mem;
        vm->stack_capacity = new_cap;
    }
    vm->stack_memory[vm->stack_pointer++] = value;
    return 0;
}

static inline int64_t vm_pop_stack(vm_virtual_machine_state_t *vm) {
    if (vm->stack_pointer == 0) {
        return 0;
    }
    return vm->stack_memory[--vm->stack_pointer];
}

int vm_execute_instruction_cycle(vm_virtual_machine_state_t *vm, const vm_instruction_t *instr) {
    if (!vm || !instr) return -1;
    
    vm->total_cycles_executed++;
    
    switch ((vm_opcode_t)instr->opcode) {
        case OP_NOOP:
            break;
            
        case OP_LOAD_CONST:
            vm->registers[instr->reg_dest] = instr->immediate_operand;
            break;
            
        case OP_MOVE_REG:
            vm->registers[instr->reg_dest] = vm->registers[instr->reg_src1];
            break;
            
        case OP_ADD_INT:
            vm->registers[instr->reg_dest] = vm->registers[instr->reg_src1] + vm->registers[instr->reg_src2];
            break;
            
        case OP_SUB_INT:
            vm->registers[instr->reg_dest] = vm->registers[instr->reg_src1] - vm->registers[instr->reg_src2];
            break;
            
        case OP_MUL_INT:
            vm->registers[instr->reg_dest] = vm->registers[instr->reg_src1] * vm->registers[instr->reg_src2];
            break;
            
        case OP_DIV_INT:
            if (vm->registers[instr->reg_src2] == 0) {
                vm->execution_halted = true;
                return -2; /* Division by zero trap */
            }
            vm->registers[instr->reg_dest] = vm->registers[instr->reg_src1] / vm->registers[instr->reg_src2];
            break;
            
        case OP_JUMP_IF_ZERO:
            if (vm->registers[instr->reg_src1] == 0) {
                vm->program_counter = (uint32_t)instr->immediate_operand;
            }
            break;
            
        case OP_JUMP_ALWAYS:
            vm->program_counter = (uint32_t)instr->immediate_operand;
            break;
            
        case OP_CALL_FUNC:
            if (vm_push_stack(vm, (int64_t)vm->program_counter) != 0) {
                return -3;
            }
            vm->program_counter = (uint32_t)instr->immediate_operand;
            break;
            
        case OP_RETURN_VAL:
            vm->program_counter = (uint32_t)vm_pop_stack(vm);
            break;
            
        case OP_HALT:
            vm->execution_halted = true;
            break;
            
        default:
            return -4; /* Unrecognized instruction opcode */
    }
    
    return 0;
}

void vm_cleanup(vm_virtual_machine_state_t *vm) {
    if (vm) {
        if (vm->stack_memory) {
            free(vm->stack_memory);
            vm->stack_memory = NULL;
        }
        vm->stack_capacity = 0;
        vm->stack_pointer = 0;
    }
}
